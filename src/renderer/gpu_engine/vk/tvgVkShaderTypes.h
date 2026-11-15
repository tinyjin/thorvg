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

#ifndef _TVG_VK_SHADER_TYPES_H_
#define _TVG_VK_SHADER_TYPES_H_

#include "tvgRender.h"

///////////////////////////////////////////////////////////////////////////////
// shader types
///////////////////////////////////////////////////////////////////////////////

// mat4x4f
struct VkShaderTypeMat4x4f
{
    float mat[16]{};

    VkShaderTypeMat4x4f();
    VkShaderTypeMat4x4f(const Matrix& transform);
    VkShaderTypeMat4x4f(size_t w, size_t h);
    void identity();
    void update(const Matrix& transform);
    void update(size_t w, size_t h);
};

struct VkShaderTypeMat4x4fBlock
{
    VkShaderTypeMat4x4f matrix;
    uint8_t _padding[256 - sizeof(VkShaderTypeMat4x4f)]{};
};

// vec4f
struct VkShaderTypeVec4f
{
    float vec[4]{};

    VkShaderTypeVec4f() {};
    VkShaderTypeVec4f(const ColorSpace colorSpace, uint8_t o);
    void update(const ColorSpace colorSpace, uint8_t o);
    void update(const RenderRegion& r);
};

// GLSL: PaintBlock { vec4 options; mat4 transform; vec4 coords; vec4 focal; }
struct VkShaderTypeGradSettings
{
    // gradient transform matrix
    VkShaderTypeMat4x4f transform;
    // linear: [0] - x1, [1] - y1, [2] - x2, [3] - y2
    // radial: [0] - cx, [1] - cy, [2] - cr
    VkShaderTypeVec4f coords;
    // radial: [0] - fx, [1] - fy, [2] - fr
    VkShaderTypeVec4f focal;
    
    void update(const Fill* fill, const Matrix* transform);
};

struct VkShaderTypePaintSettings
{
    // [0] - color space, [1] - gradient ramp row, [3] - opacity
    VkShaderTypeVec4f options;
    // gradient settings (linear/radial/conic)
    VkShaderTypeGradSettings gradient;
    // dynamic uniform offsets: 256 is the largest minUniformBufferOffsetAlignment allowed by Vulkan
    uint8_t _padding[256 - sizeof(options) - sizeof(gradient)];
};
static_assert(sizeof(VkShaderTypePaintSettings) == 256, "Uniform shader data type size must be aligned to 256 bytes");

// gradient color map
#define VK_TEXTURE_GRADIENT_SIZE 512
struct VkShaderTypeGradientData
{
    uint8_t data[VK_TEXTURE_GRADIENT_SIZE * 4];

    bool update(const Fill* fill);
};

#define VK_GAUSSIAN_KERNEL_SIZE_MAX (128.0f)  // gaussian params: sigma, scale, extend

// gaussian blur: [0]: sigma, [1]: scale, [2]: kernel size
// drop shadow:   [0]: sigma, [1]: scale, [2]: kernel size [4..7]: premultiplied RGBA, [8, 9]: offset in pixels
// fill:          [0..3]: normalized RGBA
// tint:          [0..2]: normalized black RGB, [4..6]: normalized white RGB [8]: normalized intensity
// tritone:       [0..2]: normalized shadow RGB, [4..6]: normalized midtone RGB [8..10]: normalized highlight RGB, [11]: normalized blender
// motion blur:   [0, 1]: offset in pixels, [2]: sample count, [3]: unused [4..7]: clipped compositing region (x, y, width, height) in pixels
struct VkShaderTypeEffectParams
{
    float params[4 + 4 + 4];  // push constants: vec4 params[3]
    uint32_t extend;          // gaussian blur and drop shadow extend
    Point offset;             // drop shadow and motion blur offset

    bool update(RenderEffectGaussianBlur* gaussian, const Matrix& transform);
    bool update(RenderEffectMotionBlur* motion, const Matrix& transform);
    bool update(RenderEffectDropShadow* dropShadow, const Matrix& transform);
    bool update(RenderEffectFill* fill);
    bool update(RenderEffectTint* tint);
    bool update(RenderEffectTritone* tritone);
};

#endif // _TVG_VK_SHADER_TYPES_H_
