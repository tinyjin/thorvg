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

#ifndef _TVG_MTL_RENDER_DATA_H_
#define _TVG_MTL_RENDER_DATA_H_

#include "tvgMtlMesh.h"
#include "tvgMtlShaderTypes.h"
#include "tvgMtlCommon.h"

struct MtlTextureMgr;
struct MtlStageBufferSolidColor;

struct MtlGradientTexture
{
    id<MTLTexture> texture{};
    MtlBinding binding{};

    bool update(MtlContext& context, const Fill* fill, FillSpread& currentSpread);
    void release(MtlContext& context);
};

enum class MtlRenderSettingsType { None = 0, Solid = 1, Linear = 2, Radial = 3, Conic = 4 };

static_assert(sizeof(RenderColor) == 4, "Solid color vertex data must remain tightly packed RGBA8");

struct MtlSolidFill
{
    uint32_t colorIdx;
    RenderColor color;
    uint8_t opacity;

    RenderColor packedColor() const
    {
        return {color.r, color.g, color.b, MULTIPLY(color.a, opacity)};
    }
};

struct MtlRenderSettings
{
    uint32_t paintIdx;
    MtlShaderTypePaintSettings settings;
    MtlGradientTexture gradientData;
    MtlRenderSettingsType fillType{};
    float opacityMultiplier = 1.0f;
    bool valid = false;
    bool opaque;
    FillSpread spread{};  // cached here to use existing tail padding

    uint8_t update(tvg::ColorSpace cs, uint8_t opacity);
    void update(MtlContext& context, const Fill* fill, const Matrix* transform, bool updateColorRamp);
    void release(MtlContext& context);
};

struct MtlPaint
{
    RenderRegion viewport;
    Array<MtlPaint*> clips;
    Matrix transform;

    virtual ~MtlPaint(){};
    virtual void release(MtlContext& context) = 0;
    virtual RenderRegion region() = 0;
    virtual Type type() { return Type::Undefined; };
    void assign(const Array<RenderData>& clips);
};

struct MtlShape : MtlPaint
{
    struct
    {
        MtlRenderSettings setting;
        MtlSolidFill solid;
        MtlMesh mesh;
        BBox bbox;
    } shape;

    struct
    {
        MtlRenderSettings setting;
        MtlSolidFill solid;
        MtlMesh mesh;
        MtlMesh bboxMesh;
        BBox bbox;
    } stroke;

    MtlMesh bboxMesh;
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
    void release(MtlContext& context) override;

    RenderRegion region() override
    {
        auto& min = bbox.min;
        auto& max = bbox.max;
        return {int32_t(nearbyint(min.x)), int32_t(nearbyint(min.y)), int32_t(nearbyint(max.x)), int32_t(nearbyint(max.y))};
    }

    Type type() override { return Type::Shape; };
};

struct MtlImage : MtlPaint
{
    const RenderSurface* surface{};
    MtlRenderSettings setting;
    MtlBinding binding{};
    MtlMesh mesh;
    uint16_t stamp = 0;
    FilterMethod filter = FilterMethod::Bilinear;

    void update(const RenderSurface* surface, const Matrix& transform);
    void setup(const MtlBinding& binding, const RenderSurface* surface, FilterMethod filter, uint16_t stamp);
    void release(MtlTextureMgr& textures, MtlContext& context);
    void reset();
    void release(MtlContext& context) override;

    // TODO: return an elaborate bbox
    RenderRegion region() override { return viewport; }
    Type type() override { return Type::Picture; };
};

struct MtlGeometryRange
{
    size_t vertexOffset;
    size_t indexOffset;
    uint32_t vertexCount;
    uint32_t indexCount;
};

struct MtlSolidBatchRange : MtlGeometryRange
{
    size_t colorOffset{};
    RenderRegion viewport;
};

struct MtlImageBatchRange : MtlGeometryRange
{
    size_t texCoordOffset{};
    RenderRegion viewport;
};

struct MtlStencilBatchRange
{
    MtlGeometryRange stencil;
    MtlGeometryRange cover;
    size_t colorOffset{};
    RenderRegion viewport;
    FillRule fillRule;
    bool solidOnly;
};

struct MtlRenderEffect
{
    float params[12];
    uint32_t extend;
    Point offset;

    void update(RenderEffectMotionBlur* motion, const Matrix& transform);
    void update(RenderEffectGaussianBlur* gaussian, const Matrix& transform);
    void update(RenderEffectDropShadow* dropShadow, const Matrix& transform);
    void update(RenderEffectFill* fill);
    void update(RenderEffectTint* tint);
    void update(RenderEffectTritone* tritone);
};

struct MtlRenderEffectPool
{
    // pool contains all created but unused render effects
    Array<MtlRenderEffect*> mPool;
    // list contains all created render effects to ensure that all created instances will be released
    Array<MtlRenderEffect*> mList;

    MtlRenderEffect* allocate();
    void free(MtlRenderEffect* rdata);
    void release();
};

struct MtlStageBufferGeometry
{
    Array<uint8_t> vbuffer;
    Array<uint8_t> ibuffer;
    void appendBatch(const Array<MtlShape*>& renderShapes, MtlGeometryRange& range, bool cover);

    void append(MtlMesh* meshData);
    void append(MtlShape* renderShape);
    void append(MtlImage* renderPicture);
    void appendSolidBatch(const Array<MtlShape*>& renderShapes, MtlStageBufferSolidColor& colors, MtlSolidBatchRange& range);
    void appendImageBatch(const Array<MtlImage*>& renderImages, MtlImageBatchRange& range);
    void appendStencilBatch(const Array<MtlShape*>& renderShapes, MtlStencilBatchRange& range);
    void clear();
};

struct MtlStageBufferSolidColor
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

#endif // _TVG_MTL_RENDER_DATA_H_
