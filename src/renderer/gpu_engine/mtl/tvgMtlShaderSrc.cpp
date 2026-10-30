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

#include "tvgMtlShaderSrc.h"

#define MTL_SHADER_COMMON R"MSL(
#include <metal_stdlib>
using namespace metal;

struct GradSettings { float4x4 transform; float4 coords; float4 focal; };
struct PaintSettings { float4 options; GradSettings gradient; };

struct ShapeInput { float2 position [[attribute(0)]]; float4 adjust [[attribute(2)]]; };
struct SolidInput { float2 position [[attribute(0)]]; float4 color [[attribute(1)]]; float4 adjust [[attribute(2)]]; };
struct ImageInput { float2 position [[attribute(0)]]; float2 texCoord [[attribute(1)]]; float4 adjust [[attribute(2)]]; };
struct QuadInput { float2 position [[attribute(0)]]; float2 texCoord [[attribute(1)]]; };
struct ComposeInput { float2 position [[attribute(0)]]; float2 texCoord [[attribute(1)]]; float2 maskCoord [[attribute(3)]]; };

struct PositionOutput { float4 position [[position]]; };
struct SolidOutput { float4 position [[position]]; float4 color; };
struct GradientOutput { float4 position [[position]]; float4 vGradCoord; };
struct ImageOutput { float4 position [[position]]; float2 vTexCoord; };
struct ComposeOutput { float4 position [[position]]; float2 texCoord; float2 maskCoord; };

constant float GRADIENT_SIZE = 512.0;
constant float INV_TAU = 0.15915494309189535;

static inline float4 project(float2 position, float4 adjust, float4x4 view) {
    float4 pos = view * float4(position, 0.0, 1.0);
    return float4(pos.xy * adjust.xy + adjust.zw * pos.w, pos.zw);
}

static inline float linearT(float2 pos, float4 coords) {
    float2 st = coords.xy;
    float2 ba = coords.zw - st;
    return dot(pos - st, ba) / dot(ba, ba);
}

static inline float radialT(float2 coord, float4 coords, float4 focal) {
    float2 d0 = coord - coords.xy;
    float2 d1 = coords.xy - focal.xy;
    float r0 = coords.z;
    float rd = focal.z - coords.z;
    float a = 1.0 * dot(d1, d1) - 1.0 * rd * rd;
    float b = 2.0 * dot(d0, d1) - 2.0 * r0 * rd;
    float c = 1.0 * dot(d0, d0) - 1.0 * r0 * r0;
    float d = b * b - 4.0 * a * c;
    float t = 0.0;
    if (d >= 0.0) t = min(1.0, (-b + sqrt(d)) / (2.0 * a));
    if (c > 0.0 && t >= 1.0) t = 0.0;
    return t;
}

static inline float4 conicColor(float2 pos, texture2d<float> tex, sampler smp) {
    float width = fwidth(pos.y);
    if (pos.x >= 0.0 && abs(pos.y) < 0.5 * width) {
        float4 first = tex.read(uint2(0, 0));
        float4 last = tex.read(uint2(uint(GRADIENT_SIZE) - 1, 0));
        float coverage = pos.y / width + 0.5;
        return mix(float4(last.rgb * last.a, last.a), float4(first.rgb * first.a, first.a), coverage);
    }
    float t = fract(atan2(pos.y, pos.x) * INV_TAU);
    float u = (t * (GRADIENT_SIZE - 1.0) + 0.5) / GRADIENT_SIZE;
    // Explicit LOD avoids an anisotropic footprint across the wrapped seam coordinate.
    float4 color = tex.sample(smp, float2(u, 0.5), level(0.0));
    return float4(color.rgb * color.a, color.a);
}

struct FragData { float3 Sc; float Sa; float So; float3 Dc; float Da; };

#ifdef MTL_TILED
#define DST_ARG float4 uTextureDst [[color(0)]]
#define DST_PARAM float4 uTextureDst
#define DST_READ(v) uTextureDst
#else
#define DST_ARG texture2d<float> uTextureDst [[texture(1)]]
#define DST_PARAM texture2d<float> uTextureDst
#define DST_READ(v) uTextureDst.read(uint2(v.position.xy))
#endif
)MSL"

const char* cMtlShaderSrc_Common = MTL_SHADER_COMMON;

const char* cMtlShaderSrc_Main = MTL_SHADER_COMMON R"MSL(

vertex PositionOutput vs_stencil(ShapeInput in [[stage_in]], constant float4x4& uViewMat [[buffer(4)]]) {
    return {project(in.position, in.adjust, uViewMat)};
}

vertex PositionOutput vs_depth(ShapeInput in [[stage_in]], constant float4x4& uViewMat [[buffer(4)]], constant float& uDepth [[buffer(6)]]) {
    float4 position = project(in.position, in.adjust, uViewMat);
    position.z = uDepth;
    return {position};
}

vertex SolidOutput vs_solid(SolidInput in [[stage_in]], constant float4x4& uViewMat [[buffer(4)]]) {
    return {project(in.position, in.adjust, uViewMat), in.color};
}

fragment float4 fs_solid(SolidOutput in [[stage_in]]) {
    return float4(in.color.rgb * in.color.a, in.color.a);
}

vertex GradientOutput vs_gradient(ShapeInput in [[stage_in]], constant float4x4& uViewMat [[buffer(4)]], constant PaintSettings& uPaintSettings [[buffer(5)]]) {
    return {project(in.position, in.adjust, uViewMat), uPaintSettings.gradient.transform * float4(in.position, 0.0, 1.0)};
}

fragment float4 fs_linear(GradientOutput in [[stage_in]], constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureGrad [[texture(0)]], sampler uSamplerGrad [[sampler(0)]]) {
    float t = linearT(in.vGradCoord.xy, uPaintSettings.gradient.coords);
    float4 Sc = uTextureGrad.sample(uSamplerGrad, float2(t, 0.5));
    float So = uPaintSettings.options.a;
    return float4(Sc.rgb * Sc.a * So, Sc.a * So);
}

fragment float4 fs_radial(GradientOutput in [[stage_in]], constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureGrad [[texture(0)]], sampler uSamplerGrad [[sampler(0)]]) {
    float t = radialT(in.vGradCoord.xy, uPaintSettings.gradient.coords, uPaintSettings.gradient.focal);
    float4 Sc = uTextureGrad.sample(uSamplerGrad, float2(1.0 - t, 0.5));
    float So = uPaintSettings.options.a;
    return float4(Sc.rgb * Sc.a * So, Sc.a * So);
}

fragment float4 fs_conic(GradientOutput in [[stage_in]], constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureGrad [[texture(0)]], sampler uSamplerGrad [[sampler(0)]]) {
    return conicColor(in.vGradCoord.xy, uTextureGrad, uSamplerGrad) * uPaintSettings.options.a;
}

vertex ImageOutput vs_image(ImageInput in [[stage_in]], constant float4x4& uViewMat [[buffer(4)]]) {
    return {project(in.position, in.adjust, uViewMat), in.texCoord};
}

fragment float4 fs_image(ImageOutput in [[stage_in]], constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureView [[texture(0)]], sampler uSampler [[sampler(0)]]) {
    float4 Sc = uTextureView.sample(uSampler, in.vTexCoord);
    float So = uPaintSettings.options.a;
    return float4(Sc.rgb * So, Sc.a * So);
}

vertex ImageOutput vs_quad(QuadInput in [[stage_in]]) {
    return {float4(in.position, 0.0, 1.0), in.texCoord};
}

fragment float4 fs_scene(ImageOutput in [[stage_in]], constant float& So [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    float4 Sc = uTextureSrc.sample(uSamplerSrc, in.vTexCoord);
    return float4(Sc.rgb * So, Sc.a * So);
}

vertex ComposeOutput vs_compose(ComposeInput in [[stage_in]]) {
    return {float4(in.position, 0.0, 1.0), in.texCoord, in.maskCoord};
}

#define COMPOSE(NAME, ...)                                                                                                                             \
fragment float4 fs_compose_##NAME(ComposeOutput in [[stage_in]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]],      \
                                  texture2d<float> uTextureMsk [[texture(1)]], sampler uSamplerMsk [[sampler(1)]]) {                                   \
    float4 src = uTextureSrc.sample(uSamplerSrc, in.texCoord);                                                                                         \
    float4 msk = uTextureMsk.sample(uSamplerMsk, in.maskCoord);                                                                                        \
    msk = select(float4(0.0), msk, all(in.maskCoord >= float2(0.0)) && all(in.maskCoord < float2(1.0)));                                               \
    float luma = dot(msk.rgb, float3(0.2125, 0.7154, 0.0721));                                                                                         \
    return __VA_ARGS__;                                                                                                                                \
}

COMPOSE(None, src)
COMPOSE(AlphaMask, src * msk.a)
COMPOSE(InvAlphaMask, src * (1.0 - msk.a))
COMPOSE(LumaMask, src * luma)
COMPOSE(InvLumaMask, src * (1.0 - luma))
COMPOSE(AddMask, float4(src.rgb, src.a + msk.a * (1.0 - src.a)))
COMPOSE(SubtractMask, float4(src.rgb, src.a * (1.0 - msk.a)))
COMPOSE(IntersectMask, float4(src.rgb, src.a * msk.a))
COMPOSE(DifferenceMask, float4(src.rgb, src.a * (1.0 - msk.a) + msk.a * (1.0 - src.a)))
COMPOSE(LightenMask, float4(src.rgb, max(src.a, msk.a)))
COMPOSE(DarkenMask, float4(src.rgb, min(src.a, msk.a)))

vertex PositionOutput vs_blit(uint vid [[vertex_id]]) {
    const float2 corners[] = {float2(-1.0, 1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, -1.0)};
    return {float4(corners[vid], 0.0, 1.0)};
}

fragment float4 fs_blit(PositionOutput in [[stage_in]], texture2d<float> uTextureSrc [[texture(0)]]) {
    return uTextureSrc.read(uint2(in.position.xy));
}

fragment float4 fs_blit_unpremultiplied(PositionOutput in [[stage_in]], texture2d<float> uTextureSrc [[texture(0)]]) {
    float4 src = uTextureSrc.read(uint2(in.position.xy));
    if (src.a <= 0.0) return float4(0.0);
    return float4(clamp(src.rgb / src.a, float3(0.0), float3(1.0)), src.a);
}

fragment float4 fs_shadow(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]],
                          texture2d<float> uTextureSdw [[texture(1)]], sampler uSamplerSdw [[sampler(1)]]) {
    float2 texelSize = 1.0 / float2(uTextureSrc.get_width(), uTextureSrc.get_height());
    float2 offset = settings[2].xy * texelSize;
    float4 orig = uTextureSrc.sample(uSamplerSrc, in.vTexCoord);
    float4 blur = uTextureSdw.sample(uSamplerSdw, in.vTexCoord - offset);
    float4 shad = settings[1] * blur.a;
    return orig + shad * (1.0 - orig.a);
}

static inline float gaussian(float x, float sigma) {
    float exponent = -x * x / (2.0 * sigma * sigma);
    return exp(exponent) * 0.39894 / sigma;
}

static inline float4 gaussianBlur(float2 coord, float2 axis, constant float4* settings, texture2d<float> tex, sampler smp) {
    float2 texelSize = 1.0 / float2(tex.get_width(), tex.get_height());
    float4 colorSum = float4(0.0);
    float sigma = settings[0].x * settings[0].y;
    float weightSum = 0.0;
    int radius = int(settings[0].z);
    for (int i = -radius; i <= radius; i++) {
        float2 sampled = coord + axis * float(i) * texelSize;
        float along = dot(sampled, axis);
        float weight = saturate(along) == along ? gaussian(float(i), sigma) : 0.0;
        colorSum += tex.sample(smp, sampled, level(0.0)) * weight;
        weightSum += weight;
    }
    return colorSum / weightSum;
}

fragment float4 fs_gaussian_vert(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    return gaussianBlur(in.vTexCoord, float2(0.0, 1.0), settings, uTextureSrc, uSamplerSrc);
}

fragment float4 fs_gaussian_horz(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    return gaussianBlur(in.vTexCoord, float2(1.0, 0.0), settings, uTextureSrc, uSamplerSrc);
}

fragment float4 fs_motion(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    // Clamp sampling coordinates to the region's outermost texel centers.
    float2 invTextureSize = 1.0 / float2(uTextureSrc.get_width(), uTextureSrc.get_height());
    float2 minCoord = settings[1].xy + float2(0.5);
    float2 maxCoord = settings[1].xy + settings[1].zw - float2(0.5);
    int samples = int(settings[0].z);
    float invSamples = 1.0 / float(samples);
    float4 color = float4(0.0);
    for (int i = 0; i < samples; i++) {
        float t = (float(i) + 0.5) * invSamples - 0.5;
        float2 p = clamp(in.vTexCoord / invTextureSize + settings[0].xy * t, minCoord, maxCoord);
        color += uTextureSrc.sample(uSamplerSrc, p * invTextureSize, level(0.0));
    }
    return color * invSamples;
}

fragment float4 fs_fill(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    float4 orig = uTextureSrc.sample(uSamplerSrc, in.vTexCoord);
    float4 fill = settings[0];
    return fill * orig.a * fill.a;
}

fragment float4 fs_tint(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    float4 orig = uTextureSrc.sample(uSamplerSrc, in.vTexCoord);
    float luma = dot(orig.rgb, float3(0.2126, 0.7152, 0.0722));
    return float4(mix(orig.rgb, mix(settings[0].rgb, settings[1].rgb, luma), settings[2].r) * orig.a, orig.a);
}

fragment float4 fs_tritone(ImageOutput in [[stage_in]], constant float4* settings [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]]) {
    float4 orig = uTextureSrc.sample(uSamplerSrc, in.vTexCoord);
    float luma = dot(orig.rgb, float3(0.2126, 0.7152, 0.0722));
    bool isBright = luma >= 0.5;
    float t = isBright ? (luma - 0.5) * 2.0 : luma * 2.0;
    float3 frm = isBright ? settings[1].rgb : settings[0].rgb;
    float3 to = isBright ? settings[2].rgb : settings[1].rgb;
    float4 tmp = float4(mix(frm, to, t), 1.0);
    if (settings[2].a > 0.0) tmp = mix(tmp, orig, settings[2].a);
    return tmp * orig.a;
}

kernel void cs_premult_bgr(uint2 gid [[thread_position_in_grid]], texture2d<float, access::read> uTextureSrc [[texture(0)]], texture2d<float, access::write> uTextureDst [[texture(1)]]) {
    if (gid.x >= uTextureSrc.get_width() || gid.y >= uTextureSrc.get_height()) return;
    float4 Sc = uTextureSrc.read(gid);
    uTextureDst.write(float4(Sc.bgr * Sc.a, Sc.a), gid);
}

kernel void cs_premult(uint2 gid [[thread_position_in_grid]], texture2d<float, access::read> uTextureSrc [[texture(0)]], texture2d<float, access::write> uTextureDst [[texture(1)]]) {
    if (gid.x >= uTextureSrc.get_width() || gid.y >= uTextureSrc.get_height()) return;
    float4 Sc = uTextureSrc.read(gid);
    uTextureDst.write(float4(Sc.rgb * Sc.a, Sc.a), gid);
}
)MSL";

const char* cMtlShaderSrc_Solid_Blend = R"MSL(
typedef SolidOutput VertexOutput;
#define FS_ARGS DST_ARG
#define FS_PASS uTextureDst

static inline FragData getFragData(VertexOutput v, DST_PARAM) {
    float4 colorSrc = v.color;
    float4 colorDst = DST_READ(v);
    FragData data;
    data.Sc = colorSrc.rgb * colorSrc.a;
    data.Sa = colorSrc.a;
    data.So = 1.0;
    data.Dc = colorDst.rgb;
    data.Da = colorDst.a;
    return data;
}

static inline float4 postProcess(FragData d, float4 R) { return R; }
)MSL";

const char* cMtlShaderSrc_Linear_Blend = R"MSL(
typedef GradientOutput VertexOutput;
#define FS_ARGS constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureGrad [[texture(0)]], sampler uSamplerGrad [[sampler(0)]], DST_ARG
#define FS_PASS uPaintSettings, uTextureGrad, uSamplerGrad, uTextureDst

static inline FragData getFragData(VertexOutput v, constant PaintSettings& uPaintSettings, texture2d<float> uTextureGrad, sampler uSamplerGrad, DST_PARAM) {
    float t = linearT(v.vGradCoord.xy, uPaintSettings.gradient.coords);
    float4 colorSrc = uTextureGrad.sample(uSamplerGrad, float2(t, 0.5));
    float4 colorDst = DST_READ(v);
    FragData data;
    data.Sc = colorSrc.rgb;
    data.Sa = colorSrc.a;
    data.So = uPaintSettings.options.a;
    data.Dc = colorDst.rgb;
    data.Da = colorDst.a;
    data.Sc = mix(data.Dc, data.Sc, data.Sa * data.So);
    data.Sa = mix(data.Da, 1.0, data.Sa * data.So);
    return data;
}

static inline float4 postProcess(FragData d, float4 R) { return R; }
)MSL";

const char* cMtlShaderSrc_Radial_Blend = R"MSL(
typedef GradientOutput VertexOutput;
#define FS_ARGS constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureGrad [[texture(0)]], sampler uSamplerGrad [[sampler(0)]], DST_ARG
#define FS_PASS uPaintSettings, uTextureGrad, uSamplerGrad, uTextureDst

static inline FragData getFragData(VertexOutput v, constant PaintSettings& uPaintSettings, texture2d<float> uTextureGrad, sampler uSamplerGrad, DST_PARAM) {
    float t = radialT(v.vGradCoord.xy, uPaintSettings.gradient.coords, uPaintSettings.gradient.focal);
    float4 colorSrc = uTextureGrad.sample(uSamplerGrad, float2(1.0 - t, 0.5));
    float4 colorDst = DST_READ(v);
    FragData data;
    data.Sc = colorSrc.rgb;
    data.Sa = colorSrc.a;
    data.So = uPaintSettings.options.a;
    data.Dc = colorDst.rgb;
    data.Da = colorDst.a;
    data.Sc = mix(data.Dc, data.Sc, data.Sa * data.So);
    data.Sa = mix(data.Da, 1.0, data.Sa * data.So);
    return data;
}

static inline float4 postProcess(FragData d, float4 R) { return R; }
)MSL";

const char* cMtlShaderSrc_Conic_Blend = R"MSL(
typedef GradientOutput VertexOutput;
#define FS_ARGS constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureGrad [[texture(0)]], sampler uSamplerGrad [[sampler(0)]], DST_ARG
#define FS_PASS uPaintSettings, uTextureGrad, uSamplerGrad, uTextureDst

static inline FragData getFragData(VertexOutput v, constant PaintSettings& uPaintSettings, texture2d<float> uTextureGrad, sampler uSamplerGrad, DST_PARAM) {
    float4 colorSrc = conicColor(v.vGradCoord.xy, uTextureGrad, uSamplerGrad);
    float4 colorDst = DST_READ(v);
    FragData data;
    data.Sc = colorSrc.rgb;
    data.Sa = colorSrc.a;
    data.So = uPaintSettings.options.a;
    data.Dc = colorDst.rgb;
    data.Da = colorDst.a;
    data.Sc = data.Dc * (1.0 - data.Sa * data.So) + data.Sc * data.So;
    data.Sa = mix(data.Da, 1.0, data.Sa * data.So);
    return data;
}

static inline float4 postProcess(FragData d, float4 R) { return R; }
)MSL";

const char* cMtlShaderSrc_Image_Blend = R"MSL(
typedef ImageOutput VertexOutput;
#define FS_ARGS constant PaintSettings& uPaintSettings [[buffer(0)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]], DST_ARG
#define FS_PASS uPaintSettings, uTextureSrc, uSamplerSrc, uTextureDst

static inline FragData getFragData(VertexOutput v, constant PaintSettings& uPaintSettings, texture2d<float> uTextureSrc, sampler uSamplerSrc, DST_PARAM) {
    float4 colorSrc = uTextureSrc.sample(uSamplerSrc, v.vTexCoord);
    float4 colorDst = DST_READ(v);
    FragData data;
    data.So = uPaintSettings.options.a;
    data.Sc = colorSrc.rgb * data.So;
    data.Sa = colorSrc.a * data.So;
    data.Dc = colorDst.rgb;
    data.Da = colorDst.a;
    return data;
}

static inline float4 postProcess(FragData d, float4 R) { return mix(float4(d.Dc, d.Da), R, d.Sa); }
)MSL";

const char* cMtlShaderSrc_Scene_Blend = R"MSL(
typedef ImageOutput VertexOutput;
#define FS_ARGS constant float& So [[buffer(1)]], texture2d<float> uTextureSrc [[texture(0)]], sampler uSamplerSrc [[sampler(0)]], DST_ARG
#define FS_PASS So, uTextureSrc, uSamplerSrc, uTextureDst

static inline FragData getFragData(VertexOutput v, float So, texture2d<float> uTextureSrc, sampler uSamplerSrc, DST_PARAM) {
    float4 colorSrc = uTextureSrc.sample(uSamplerSrc, v.vTexCoord);
    float4 colorDst = DST_READ(v);
    FragData data;
    data.Sc = min(float3(1.0), colorSrc.rgb / ((colorSrc.a == 0.0 || colorSrc.a == 1.0) ? 1.0 : colorSrc.a));
    data.Sa = colorSrc.a;
    data.So = So;
    data.Dc = colorDst.rgb;
    data.Da = colorDst.a;
    return data;
}

static inline float4 postProcess(FragData d, float4 R) { return mix(float4(d.Dc, d.Da), R, d.Sa * d.So); }
)MSL";

const char* cMtlShaderSrc_BlendFuncs = R"MSL(
constant float3 One = float3(1.0, 1.0, 1.0);
constant float3 LUM_W = float3(0.3, 0.59, 0.11);

float3 setLum(float3 colorIn, float l) {
    float3 color = colorIn + float3(l - dot(colorIn, LUM_W));
    float ll = dot(color, LUM_W);
    float n = min(color.r, min(color.g, color.b));
    float x = max(color.r, max(color.g, color.b));
    if (n < 0.0) color = float3(ll) + (color - float3(ll)) * (ll / (ll - n));
    if (x > 1.0) color = float3(ll) + (color - float3(ll)) * ((1.0 - ll) / (x - ll));
    return color;
}

float sat(float3 color) {
    return max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
}

float3 setSat(float3 colorIn, float s) {
    float rMin = step(colorIn.r, colorIn.g) * step(colorIn.r, colorIn.b);
    float gMin = (1.0 - rMin) * step(colorIn.g, colorIn.r) * step(colorIn.g, colorIn.b);
    float3 minMask = float3(rMin, gMin, 1.0 - rMin - gMin);

    float bMax = step(colorIn.r, colorIn.b) * step(colorIn.g, colorIn.b);
    float gMax = (1.0 - bMax) * step(colorIn.r, colorIn.g) * step(colorIn.b, colorIn.g);
    float3 maxMask = float3(1.0 - bMax - gMax, gMax, bMax);
    float3 midMask = float3(1.0) - minMask - maxMask;

    float cMin = dot(colorIn, minMask);
    float cMid = dot(colorIn, midMask);
    float cMax = dot(colorIn, maxMask);
    float delta = cMax - cMin;
    float deltaMask = sign(delta);
    float scale = deltaMask * s / max(delta, 1e-6);

    return maxMask * float3(s * deltaMask) + midMask * float3((cMid - cMin) * scale);
}

#define BLEND(NAME, ...)                                                       \
fragment float4 fs_main_##NAME(VertexOutput v [[stage_in]], FS_ARGS) {         \
    FragData d = getFragData(v, FS_PASS);                                      \
    float3 Rc = d.Sc;                                                          \
    __VA_ARGS__                                                                \
    return postProcess(d, float4(Rc, 1.0));                                    \
}

fragment float4 fs_main_Normal(VertexOutput v [[stage_in]]) {
    // used as debug blend method
    return float4(1.0, 0.0, 0.0, 1.0);
}

BLEND(Multiply, {
    if (d.Da > 0.0) {
        Rc = d.Sc * min(One, d.Dc / d.Da);
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Overlay, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc.r = select(1.0 - min(1.0, 2.0 * (1.0 - d.Sc.r) * (1.0 - Dc.r)), min(1.0, 2.0 * d.Sc.r * Dc.r), (Dc.r < 0.5));
        Rc.g = select(1.0 - min(1.0, 2.0 * (1.0 - d.Sc.g) * (1.0 - Dc.g)), min(1.0, 2.0 * d.Sc.g * Dc.g), (Dc.g < 0.5));
        Rc.b = select(1.0 - min(1.0, 2.0 * (1.0 - d.Sc.b) * (1.0 - Dc.b)), min(1.0, 2.0 * d.Sc.b * Dc.b), (Dc.b < 0.5));
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Darken, {
    if (d.Da > 0.0) {
        Rc = min(d.Sc, min(One, d.Dc / d.Da));
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Lighten, {
    Rc = max(d.Sc, d.Dc);
})

BLEND(ColorDodge, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc.r = select(0.0, select(1.0, min(1.0, Dc.r / (1.0 - d.Sc.r)), d.Sc.r < 1.0), Dc.r > 0.0);
        Rc.g = select(0.0, select(1.0, min(1.0, Dc.g / (1.0 - d.Sc.g)), d.Sc.g < 1.0), Dc.g > 0.0);
        Rc.b = select(0.0, select(1.0, min(1.0, Dc.b / (1.0 - d.Sc.b)), d.Sc.b < 1.0), Dc.b > 0.0);
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(ColorBurn, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc.r = select(select(1.0, 0.0, Dc.r < 1.0), 1.0 - min(1.0, (1.0 - Dc.r) / d.Sc.r), d.Sc.r > 0.0);
        Rc.g = select(select(1.0, 0.0, Dc.g < 1.0), 1.0 - min(1.0, (1.0 - Dc.g) / d.Sc.g), d.Sc.g > 0.0);
        Rc.b = select(select(1.0, 0.0, Dc.b < 1.0), 1.0 - min(1.0, (1.0 - Dc.b) / d.Sc.b), d.Sc.b > 0.0);
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(HardLight, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc.r = select(1.0 - min(1.0, 2.0 * (1.0 - d.Sc.r) * (1.0 - Dc.r)), min(1.0, 2.0 * d.Sc.r * Dc.r), (d.Sc.r < 0.5));
        Rc.g = select(1.0 - min(1.0, 2.0 * (1.0 - d.Sc.g) * (1.0 - Dc.g)), min(1.0, 2.0 * d.Sc.g * Dc.g), (d.Sc.g < 0.5));
        Rc.b = select(1.0 - min(1.0, 2.0 * (1.0 - d.Sc.b) * (1.0 - Dc.b)), min(1.0, 2.0 * d.Sc.b * Dc.b), (d.Sc.b < 0.5));
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(SoftLight, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        auto Dlow = ((16.0 * Dc - 12.0) * Dc + 4.0) * Dc;
        auto Dhigh = sqrt(Dc);
        auto D = select(Dhigh, Dlow, Dc <= float3(0.25));
        auto low = Dc - (1.0 - 2.0 * d.Sc) * Dc * (1.0 - Dc);
        auto high = Dc + (2.0 * d.Sc - 1.0) * (D - Dc);
        Rc = select(high, low, d.Sc <= float3(0.5));
        Rc = clamp(Rc, float3(0.0), One);
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Difference, {
    Rc = abs(d.Dc - d.Sc);
})

BLEND(Hue, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc = setSat(d.Sc, sat(Dc));
        Rc = setLum(Rc, dot(Dc, LUM_W));
        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Saturation, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        auto s = max(d.Sc.r, max(d.Sc.g, d.Sc.b)) - min(d.Sc.r, min(d.Sc.g, d.Sc.b));
        auto n = min(Dc.r, min(Dc.g, Dc.b));
        auto x = max(Dc.r, max(Dc.g, Dc.b));
        Rc = float3(0.0);
        if (x > n) {
            Rc = (Dc - float3(n)) * (s / (x - n));
        }
        Rc = setLum(Rc, dot(Dc, LUM_W));

        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Color, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc = setLum(d.Sc, dot(Dc, LUM_W));

        Rc = mix(d.Sc, Rc, d.Da);
    }
})

BLEND(Luminosity, {
    if (d.Da > 0.0) {
        auto Dc = min(One, d.Dc / d.Da);
        Rc = setLum(Dc, dot(d.Sc, LUM_W));

        Rc = mix(d.Sc, Rc, d.Da);
    }
})
)MSL";

