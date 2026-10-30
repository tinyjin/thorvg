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
#include <algorithm>
#include "tvgCommon.h"
#include "tvgMath.h"
#include "tvgMtlTextureMgr.h"
#include "tvgMtlTessellator.h"
#include "tvgMtlShaderTypes.h"
#include "tvgMtlRenderData.h"

//***********************************************************************
// MtlGradientTexture
//***********************************************************************

bool MtlGradientTexture::update(MtlContext& context, const Fill* fill, FillSpread& currentSpread)
{
    // compute gradient data
    MtlShaderTypeGradientData gradientData;
    auto opaque = gradientData.update(fill);
    auto bytesPerRow = MTL_TEXTURE_GRADIENT_SIZE * sizeof(uint32_t);
    bool texHandleChanged = context.allocateTexture(texture, MTL_TEXTURE_GRADIENT_SIZE, 1, MTLPixelFormatRGBA8Unorm, gradientData.data, bytesPerRow);
    // get sampler by spread type. Conic gradients always use repeat to filter
    // across the first/last ramp texels at their seam.
    auto spread = fill->type() == Type::ConicGradient ? FillSpread::Repeat : fill->spread();
    if (texHandleChanged || currentSpread != spread) {
        auto sampler = context.samplerLinearClamp;
        if (spread == FillSpread::Reflect) sampler = context.samplerLinearMirror;
        if (spread == FillSpread::Repeat) sampler = context.samplerLinearRepeat;
        binding = {texture, sampler};
        currentSpread = spread;
    }
    return opaque;
};

void MtlGradientTexture::release(MtlContext& context)
{
    context.releaseTexture(texture);
    binding = {};
};

//***********************************************************************
// MtlRenderSettings
//***********************************************************************

uint8_t MtlRenderSettings::update(tvg::ColorSpace cs, uint8_t opacity)
{
    auto effectiveOpacity = static_cast<uint8_t>(opacity * opacityMultiplier);
    settings.options.update(cs, effectiveOpacity);
    return effectiveOpacity;
}

void MtlRenderSettings::update(MtlContext& context, const Fill* fill, const Matrix* transform, bool updateColorRamp)
{
    settings.gradient.update(fill, transform);
    if (updateColorRamp) opaque = gradientData.update(context, fill, spread);
    if (fill->type() == Type::LinearGradient)
        fillType = MtlRenderSettingsType::Linear;
    else if (fill->type() == Type::RadialGradient)
        fillType = MtlRenderSettingsType::Radial;
    else if (fill->type() == Type::ConicGradient)
        fillType = MtlRenderSettingsType::Conic;
};


void MtlRenderSettings::release(MtlContext& context)
{
    gradientData.release(context);
};

//***********************************************************************
// MtlPaint
//***********************************************************************

void MtlPaint::assign(const Array<RenderData>& clips)
{
    // trick: RenderData == MtlPaint*, just copy it.
    this->clips = *((Array<MtlPaint*>*)&clips);
}

//***********************************************************************
// MtlShape
//***********************************************************************

void MtlShape::expand(const BBox& bbox)
{
    this->bbox.min = tvg::min(this->bbox.min, bbox.min);
    this->bbox.max = tvg::max(this->bbox.max, bbox.max);
}

void MtlShape::update(const RenderShape& rshape, const RenderRegion& vport, uint8_t shapeOpacity, uint8_t strokeOpacity, uint8_t opacity)
{
    shape.setting.valid = rshape.fill || (rshape.color.a * opacity > 0);
    stroke.setting.valid = rshape.stroke && (rshape.stroke->fill || (rshape.stroke->color.a * opacity > 0));

    shape.solid.opacity = shapeOpacity;
    stroke.solid.opacity = strokeOpacity;

    fillRule = rshape.rule;
    viewport = vport;
}

void MtlShape::update(const RenderShape& rshape, const Matrix& transform, RenderUpdateFlag flag)
{
    reset();  // Optimize: bad idea to reset meshes always. it could re-use the meshes if there haven't been any path changes.

    convex = false;
    strokeFirst = rshape.strokeFirst();
    shape.setting.opacityMultiplier = 1.0f;
    stroke.setting.opacityMultiplier = 1.0f;

    // optimize path
    auto& optPath = RenderPath::scratch();
    RenderPath optStrokePath;
    bool optPathThin = false;
    bool optPathSkipFill = false;
    auto strokeWidth = rshape.strokeWidth();
    auto localOut = (std::isfinite(strokeWidth) && !tvg::zero(strokeWidth)) ? &optStrokePath : nullptr;
    if (rshape.trimpath()) {
        auto& trimmed = RenderPath::scratch();
        if (rshape.stroke->trim.trim(rshape.path, trimmed)) {
            GpuOptimizeResult result{&optPath, localOut};
            gpuOptimize(trimmed, result, transform);
            optPathThin = result.thin;
            optPathSkipFill = result.skipFill;
        }
        else optPath.clear();
    } else {
        GpuOptimizeResult result{&optPath, localOut};
        gpuOptimize(rshape.path, result, transform);
        optPathThin = result.thin;
        optPathSkipFill = result.skipFill;
    }

    auto updatePath = flag & (RenderUpdateFlag::Transform | RenderUpdateFlag::Path);

    // update fill shapes
    if (updatePath || (flag & (RenderUpdateFlag::Color | RenderUpdateFlag::Gradient))) {
        if (optPathSkipFill) {
            // Too-thin fills are suppressed instead of going through thin fallback.
            shape.mesh.clear();
        } else {
            BBox bbox;
            // Drawable thin fills are tessellated as a minimal-width stroke.
            if (optPathThin && tvg::zero(rshape.strokeWidth())) {
                MtlStroker stroker(&shape.mesh, MIN_MTL_STROKE_WIDTH, StrokeCap::Butt, StrokeJoin::Bevel);
                stroker.run(optPath, true);
                bbox = stroker.getBBox();
                shape.setting.opacityMultiplier = MIN_MTL_STROKE_ALPHA;
            } else {
                MtlBWTessellator bwTess{&shape.mesh};
                bwTess.tessellate(optPath);
                convex = bwTess.convex;
                bbox = bwTess.getBBox();
            }
            if (shape.mesh.ibuffer.empty()) {
                shape.mesh.clear();
            } else {
                shape.bbox = bbox;
                expand(bbox);
            }
        }
    }
    // update strokes shapes
    if (rshape.stroke && (updatePath || (flag & (RenderUpdateFlag::Stroke | RenderUpdateFlag::GradientStroke)))) {
        auto qualityScale = scaling(transform);
        auto strokeWidthWorld = strokeWidth * qualityScale;
        if (!std::isfinite(strokeWidthWorld)) strokeWidthWorld = strokeWidth;
        if (!std::isfinite(strokeWidthWorld)) strokeWidthWorld = 0.0f;
        if (!std::isfinite(qualityScale) || tvg::zero(qualityScale)) qualityScale = 1.0f;

        //run stroking only if it's valid
        if (!tvg::zero(strokeWidthWorld)) {
            MtlStroker stroker(&stroke.mesh, strokeWidth, rshape.strokeCap(), rshape.strokeJoin(), rshape.strokeMiterlimit(), qualityScale);
            auto& dashed = RenderPath::scratch();
            if (gpuStrokeDash(rshape, dashed, nullptr)) stroker.run(dashed);
            else stroker.run(optStrokePath);
            strokeOverlapFree = stroker.overlapFree;
            stroke.setting.opacityMultiplier = 1.0f;
            if (stroke.mesh.ibuffer.empty()) {
                stroke.mesh.clear();
            } else {
                auto bbox = stroker.getBBox();
                stroke.bbox = bbox;
                stroke.bboxMesh.bbox(bbox.min, bbox.max);
                auto strokeBounds = gpuTransformBounds(stroker.bounds(), transform);
                expand({{(float)strokeBounds.xMin, (float)strokeBounds.yMin}, {(float)strokeBounds.xMax, (float)strokeBounds.yMax}});
            }
        }
    }
    // update shapes bbox (with empty path handling)
    if (shape.mesh.vbuffer.empty() && stroke.mesh.vbuffer.empty()) bbox = {{0, 0}, {0, 0}};
    bboxMesh.bbox(bbox.min, bbox.max);
}

void MtlShape::reset()
{
    clips.clear();
    stroke.mesh.clear();
    stroke.bboxMesh.clear();
    stroke.bbox.init();
    shape.mesh.clear();
    shape.bbox.init();
    bboxMesh.clear();
    bbox.init();
}

void MtlShape::release(MtlContext& context)
{
    reset();
    stroke.setting.release(context);
    shape.setting.release(context);
};

//***********************************************************************
// MtlImage
//***********************************************************************

void MtlImage::update(const RenderSurface* surface, const Matrix& transform)
{
    mesh.imageBox(surface->w, surface->h, transform);
}

void MtlImage::setup(const MtlBinding& binding, const RenderSurface* surface, FilterMethod filter, uint16_t stamp)
{
    this->binding = binding;
    this->surface = binding.texture ? surface : nullptr;
    this->filter = filter;
    this->stamp = binding.texture ? stamp : 0;
}

void MtlImage::release(MtlTextureMgr& textures, MtlContext& context)
{
    if (binding.texture && stamp == textures.stamp) textures.release(context, surface, filter, binding.texture);
    reset();
}

void MtlImage::reset()
{
    this->binding = {};
    this->surface = nullptr;
    this->filter = FilterMethod::Bilinear;
    this->stamp = 0;
}

void MtlImage::release(MtlContext& context)
{
    clips.clear();
    setting.release(context);
    reset();
}

//***********************************************************************
// MtlRenderEffect
//***********************************************************************

void MtlRenderEffect::update(RenderEffectGaussianBlur* gaussian, const Matrix& transform)
{
    MtlShaderTypeEffectParams shaderParams;
    if (!shaderParams.update(gaussian, transform)) return;
    memcpy(params, shaderParams.params, sizeof(params));
    extend = shaderParams.extend;
}

void MtlRenderEffect::update(RenderEffectDropShadow* dropShadow, const Matrix& transform)
{
    MtlShaderTypeEffectParams shaderParams;
    if (!shaderParams.update(dropShadow, transform)) return;
    memcpy(params, shaderParams.params, sizeof(params));
    extend = shaderParams.extend;
    offset = shaderParams.offset;
}

void MtlRenderEffect::update(RenderEffectFill* fill)
{
    MtlShaderTypeEffectParams shaderParams;
    if (!shaderParams.update(fill)) return;
    memcpy(params, shaderParams.params, sizeof(params));
}

void MtlRenderEffect::update(RenderEffectTint* tint)
{
    MtlShaderTypeEffectParams shaderParams;
    if (!shaderParams.update(tint)) return;
    memcpy(params, shaderParams.params, sizeof(params));
}

void MtlRenderEffect::update(RenderEffectTritone* tritone)
{
    MtlShaderTypeEffectParams shaderParams;
    if (!shaderParams.update(tritone)) return;
    memcpy(params, shaderParams.params, sizeof(params));
}

void MtlRenderEffect::update(RenderEffectMotionBlur* motion, const Matrix& transform)
{
    MtlShaderTypeEffectParams shaderParams;
    if (!shaderParams.update(motion, transform)) return;
    memcpy(params, shaderParams.params, sizeof(params));
    offset = shaderParams.offset;
}

//***********************************************************************
// MtlRenderEffectPool
//***********************************************************************

MtlRenderEffect* MtlRenderEffectPool::allocate()
{
    MtlRenderEffect* rdata{};
    if (mPool.count > 0) {
        rdata = mPool.pick();
    } else {
        rdata = new MtlRenderEffect();
        mList.push(rdata);
    }
    return rdata;
}

void MtlRenderEffectPool::free(MtlRenderEffect* rdata)
{
    if (rdata) mPool.push(rdata);
}

void MtlRenderEffectPool::release()
{
    ARRAY_FOREACH(p, mList) {
        delete(*p);
    }
    mPool.clear();
    mList.clear();
}

//***********************************************************************
// MtlStageBufferGeometry
//***********************************************************************

void MtlStageBufferGeometry::append(MtlMesh* meshData)
{
    assert(meshData);
    uint32_t vsize = meshData->vbuffer.count * sizeof(meshData->vbuffer[0]);
    uint32_t tsize = meshData->tbuffer.count * sizeof(meshData->tbuffer[0]);
    uint32_t isize = meshData->ibuffer.count * sizeof(meshData->ibuffer[0]);
    // append vertex data
    if (vbuffer.reserved < vbuffer.count + vsize)
        vbuffer.grow(std::max(vsize, vbuffer.reserved));
    if (meshData->vbuffer.count > 0) {
        meshData->voffset = vbuffer.count;
        memcpy(vbuffer.data + vbuffer.count, meshData->vbuffer.data, vsize);
        vbuffer.count += vsize;
    }
    // append tex coords data
    if (vbuffer.reserved < vbuffer.count + tsize)
        vbuffer.grow(std::max(tsize, vbuffer.reserved));
    if (meshData->tbuffer.count > 0) {
        meshData->toffset = vbuffer.count;
        memcpy(vbuffer.data + vbuffer.count, meshData->tbuffer.data, tsize);
        vbuffer.count += tsize;
    }
    // append index data
    if (ibuffer.reserved < ibuffer.count + isize)
        ibuffer.grow(std::max(isize, ibuffer.reserved));
    if (meshData->ibuffer.count > 0) {
        meshData->ioffset = ibuffer.count;
        memcpy(ibuffer.data + ibuffer.count, meshData->ibuffer.data, isize);
        ibuffer.count += isize;
    }
}

void MtlStageBufferGeometry::append(MtlShape* renderShape)
{
    append(&renderShape->shape.mesh);
    append(&renderShape->stroke.mesh);
    append(&renderShape->stroke.bboxMesh);
    append(&renderShape->bboxMesh);
}

void MtlStageBufferGeometry::append(MtlImage* renderPicture)
{
    append(&renderPicture->mesh);
}

void MtlStageBufferGeometry::appendSolidBatch(const Array<MtlShape*>& renderShapes, MtlStageBufferSolidColor& colors, MtlSolidBatchRange& range)
{
    appendBatch(renderShapes, range, false);
    if (colors.vbuffer.reserved < colors.vbuffer.count + range.vertexCount)
        colors.vbuffer.grow(std::max(range.vertexCount, colors.vbuffer.reserved));
    range.colorOffset = colors.vbuffer.count * sizeof(RenderColor);

    ARRAY_FOREACH(shape, renderShapes) {
        const auto& mesh = (*shape)->shape.mesh;
        colors.appendRepeated((*shape)->shape.solid.packedColor(), mesh.vbuffer.count);
    }
}

void MtlStageBufferGeometry::appendBatch(const Array<MtlShape*>& renderShapes, MtlGeometryRange& range, bool cover)
{
    assert(renderShapes.count > 1);

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    ARRAY_FOREACH(p, renderShapes) {
        const auto& mesh = cover ? (*p)->bboxMesh : (*p)->shape.mesh;
        vertexCount += mesh.vbuffer.count;
        indexCount += mesh.ibuffer.count;
    }

    const uint32_t vertexBytes = vertexCount * sizeof(Point);
    const uint32_t indexBytes = indexCount * sizeof(uint32_t);

    if (vbuffer.reserved < vbuffer.count + vertexBytes)
        vbuffer.grow(std::max(vertexBytes, vbuffer.reserved));
    if (ibuffer.reserved < ibuffer.count + indexBytes)
        ibuffer.grow(std::max(indexBytes, ibuffer.reserved));

    range.vertexOffset = vbuffer.count;
    range.indexOffset = ibuffer.count;
    range.vertexCount = vertexCount;
    range.indexCount = indexCount;

    uint32_t baseVertex = 0;
    auto vertexDst = vbuffer.data + vbuffer.count;
    auto indexDst = ibuffer.data + ibuffer.count;
    ARRAY_FOREACH(p, renderShapes) {
        const auto& mesh = cover ? (*p)->bboxMesh : (*p)->shape.mesh;
        const uint32_t meshVertexBytes = mesh.vbuffer.count * sizeof(Point);
        memcpy(vertexDst, mesh.vbuffer.data, meshVertexBytes);
        vertexDst += meshVertexBytes;

        for (uint32_t i = 0; i < mesh.ibuffer.count; ++i) {
            const auto index = mesh.ibuffer[i] + baseVertex;
            memcpy(indexDst, &index, sizeof(index));
            indexDst += sizeof(index);
        }

        baseVertex += mesh.vbuffer.count;
    }
    vbuffer.count += vertexBytes;
    ibuffer.count += indexBytes;
}

void MtlStageBufferGeometry::appendStencilBatch(const Array<MtlShape*>& renderShapes, MtlStencilBatchRange& range)
{
    appendBatch(renderShapes, range.stencil, false);
    appendBatch(renderShapes, range.cover, true);
}

void MtlStageBufferGeometry::appendImageBatch(const Array<MtlImage*>& renderImages, MtlImageBatchRange& range)
{
    assert(renderImages.count > 1);

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    ARRAY_FOREACH(p, renderImages) {
        const auto& mesh = (*p)->mesh;
        assert(mesh.vbuffer.count == mesh.tbuffer.count);
        vertexCount += mesh.vbuffer.count;
        indexCount += mesh.ibuffer.count;
    }

    const uint32_t vertexBytes = vertexCount * sizeof(Point);
    const uint32_t indexBytes = indexCount * sizeof(uint32_t);

    if (vbuffer.reserved < vbuffer.count + vertexBytes * 2)
        vbuffer.grow(std::max(vertexBytes * 2, vbuffer.reserved));
    if (ibuffer.reserved < ibuffer.count + indexBytes)
        ibuffer.grow(std::max(indexBytes, ibuffer.reserved));

    range.vertexOffset = vbuffer.count;
    range.texCoordOffset = vbuffer.count + vertexBytes;
    range.indexOffset = ibuffer.count;
    range.vertexCount = vertexCount;
    range.indexCount = indexCount;

    uint32_t baseVertex = 0;
    auto vertexDst = vbuffer.data + range.vertexOffset;
    auto texCoordDst = vbuffer.data + range.texCoordOffset;
    auto indexDst = ibuffer.data + range.indexOffset;
    ARRAY_FOREACH(p, renderImages) {
        const auto& mesh = (*p)->mesh;
        const uint32_t meshVertexBytes = mesh.vbuffer.count * sizeof(Point);
        memcpy(vertexDst, mesh.vbuffer.data, meshVertexBytes);
        memcpy(texCoordDst, mesh.tbuffer.data, meshVertexBytes);
        vertexDst += meshVertexBytes;
        texCoordDst += meshVertexBytes;

        for (uint32_t i = 0; i < mesh.ibuffer.count; ++i) {
            const auto index = mesh.ibuffer[i] + baseVertex;
            memcpy(indexDst, &index, sizeof(index));
            indexDst += sizeof(index);
        }

        baseVertex += mesh.vbuffer.count;
    }
    vbuffer.count += vertexBytes * 2;
    ibuffer.count += indexBytes;
}

void MtlStageBufferGeometry::clear()
{
    vbuffer.clear();
    ibuffer.clear();
}


//***********************************************************************
// MtlStageBufferSolidColor
//***********************************************************************

void MtlStageBufferSolidColor::clear()
{
    vbuffer.clear();
}

void MtlStageBufferSolidColor::appendRepeated(const RenderColor& value, uint32_t count)
{
    if (vbuffer.reserved < vbuffer.count + count) {
        vbuffer.grow(std::max(count, vbuffer.reserved));
    }
    auto dst = vbuffer.data + vbuffer.count;
    for (uint32_t i = 0; i < count; ++i) {
        dst[i] = value;
    }
    vbuffer.count += count;
}
