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
#include "tvgMtlPipelines.h"
#include "tvgMtlRenderData.h"
#include <cstring>

#define MTL_DEPTH_STENCIL_FORMAT MTLPixelFormatDepth32Float_Stencil8
#define MTL_TARGET_FORMAT MTLPixelFormatRGBA8Unorm

enum class Layout : uint8_t { Shape, Solid, SolidBatch, Image, Quad, Compose };
enum class Output : uint8_t { Src, Normal };

struct StencilFace
{
    MTLCompareFunction compare;
    MTLStencilOperation op;
};

struct DepthStencil
{
    MTLCompareFunction depthCompare;
    bool depthWrite;
    StencilFace front;
    StencilFace back;
};

struct PipelineInfo
{
    const char* vs;
    const char* fs;
    Layout layout;
    Output output;
    const DepthStencil* depthStencil;
};

static const StencilFace _keep{MTLCompareFunctionAlways, MTLStencilOperationKeep};
static const StencilFace _zero{MTLCompareFunctionAlways, MTLStencilOperationZero};
static const StencilFace _shape{MTLCompareFunctionNotEqual, MTLStencilOperationZero};
static const StencilFace _replace{MTLCompareFunctionAlways, MTLStencilOperationReplace};
static const StencilFace _invert{MTLCompareFunctionAlways, MTLStencilOperationInvert};

static const DepthStencil _dsNonZero{MTLCompareFunctionAlways, false, {MTLCompareFunctionAlways, MTLStencilOperationIncrementWrap}, {MTLCompareFunctionAlways, MTLStencilOperationDecrementWrap}};
static const DepthStencil _dsEvenOdd{MTLCompareFunctionAlways, false, _invert, _invert};
static const DepthStencil _dsDirect{MTLCompareFunctionAlways, false, _replace, _replace};
static const DepthStencil _dsCopyStencilToDepth{MTLCompareFunctionAlways, true, _shape, _shape};
static const DepthStencil _dsCopyStencilToDepthInt{MTLCompareFunctionGreater, true, _shape, _shape};
static const DepthStencil _dsCopyDepthToStencil{MTLCompareFunctionEqual, false, _replace, _replace};
static const DepthStencil _dsMergeDepthStencil{MTLCompareFunctionEqual, true, _keep, _keep};
static const DepthStencil _dsClearDepth{MTLCompareFunctionAlways, true, _keep, _keep};
static const DepthStencil _dsShape{MTLCompareFunctionAlways, false, _shape, _shape};
static const DepthStencil _dsScene{MTLCompareFunctionAlways, false, _zero, _zero};
static const DepthStencil _dsConv{MTLCompareFunctionAlways, false, _keep, _keep};
static const DepthStencil _dsClippedStroke{MTLCompareFunctionEqual, false, _keep, _keep};

static const PipelineInfo _pipelineInfos[(uint32_t)MtlPipe::Count]{
    {"vs_stencil", nullptr, Layout::Shape, Output::Src, &_dsNonZero},
    {"vs_stencil", nullptr, Layout::Shape, Output::Src, &_dsEvenOdd},
    {"vs_stencil", nullptr, Layout::Shape, Output::Src, &_dsDirect},
    {"vs_depth", nullptr, Layout::Shape, Output::Src, &_dsCopyStencilToDepth},
    {"vs_depth", nullptr, Layout::Shape, Output::Src, &_dsCopyStencilToDepthInt},
    {"vs_depth", nullptr, Layout::Shape, Output::Src, &_dsCopyDepthToStencil},
    {"vs_depth", nullptr, Layout::Shape, Output::Src, &_dsMergeDepthStencil},
    {"vs_depth", nullptr, Layout::Shape, Output::Src, &_dsClearDepth},
    {"vs_solid", "fs_solid", Layout::Solid, Output::Normal, &_dsShape},
    {"vs_gradient", "fs_radial", Layout::Shape, Output::Normal, &_dsShape},
    {"vs_gradient", "fs_linear", Layout::Shape, Output::Normal, &_dsShape},
    {"vs_gradient", "fs_conic", Layout::Shape, Output::Normal, &_dsShape},
    {"vs_solid", "fs_solid", Layout::Solid, Output::Normal, &_dsConv},
    {"vs_solid", "fs_solid", Layout::SolidBatch, Output::Normal, &_dsScene},
    {"vs_solid", "fs_solid", Layout::SolidBatch, Output::Normal, &_dsShape},
    {"vs_gradient", "fs_radial", Layout::Shape, Output::Normal, &_dsConv},
    {"vs_gradient", "fs_linear", Layout::Shape, Output::Normal, &_dsConv},
    {"vs_gradient", "fs_conic", Layout::Shape, Output::Normal, &_dsConv},
    {"vs_image", "fs_image", Layout::Image, Output::Normal, &_dsShape},
    {"vs_image", "fs_image", Layout::Image, Output::Normal, &_dsScene},
    {"vs_quad", "fs_scene", Layout::Quad, Output::Normal, &_dsScene},
    {"vs_quad", "fs_gaussian_vert", Layout::Quad, Output::Src, nullptr},
    {"vs_quad", "fs_gaussian_horz", Layout::Quad, Output::Src, nullptr},
    {"vs_quad", "fs_shadow", Layout::Quad, Output::Src, nullptr},
    {"vs_quad", "fs_fill", Layout::Quad, Output::Src, nullptr},
    {"vs_quad", "fs_tint", Layout::Quad, Output::Src, nullptr},
    {"vs_quad", "fs_tritone", Layout::Quad, Output::Src, nullptr},
    {"vs_quad", "fs_motion", Layout::Quad, Output::Src, nullptr}
};

static const char* _blendNames[]{
    "fs_main_Normal",
    "fs_main_Multiply",
    nullptr,  // Screen: hardware blending
    "fs_main_Overlay",
    "fs_main_Darken",
    "fs_main_Lighten",
    "fs_main_ColorDodge",
    "fs_main_ColorBurn",
    "fs_main_HardLight",
    "fs_main_SoftLight",
    "fs_main_Difference",
    nullptr,  // Exclusion: hardware blending
    "fs_main_Hue",
    "fs_main_Saturation",
    "fs_main_Color",
    "fs_main_Luminosity",
    nullptr,  // Add: hardware blending
    "fs_main_Normal"  // TODO: a padding for reserved Hardmix.
};

static const char* _composeNames[]{
    "fs_compose_None",
    "fs_compose_AlphaMask",
    "fs_compose_InvAlphaMask",
    "fs_compose_LumaMask",
    "fs_compose_InvLumaMask",
    "fs_compose_AddMask",
    "fs_compose_SubtractMask",
    "fs_compose_IntersectMask",
    "fs_compose_DifferenceMask",
    "fs_compose_LightenMask",
    "fs_compose_DarkenMask"
};

static MTLVertexDescriptor* _vertexDescriptor(Layout layout)
{
    auto desc = [MTLVertexDescriptor vertexDescriptor];
    auto attribute = [desc](uint32_t location, MTLVertexFormat format, uint32_t slot, uint32_t stride, MTLVertexStepFunction step) {
        desc.attributes[location].format = format;
        desc.attributes[location].offset = 0;
        desc.attributes[location].bufferIndex = slot;
        desc.layouts[slot].stride = stride;
        desc.layouts[slot].stepFunction = step;
        desc.layouts[slot].stepRate = 1;
    };
    attribute(0, MTLVertexFormatFloat2, MtlSlotPosition, 8, MTLVertexStepFunctionPerVertex);
    if (layout != Layout::Quad && layout != Layout::Compose) attribute(2, MTLVertexFormatFloat4, MtlSlotAdjust, 16, MTLVertexStepFunctionPerInstance);
    switch (layout) {
        // Solid colors advance per instance for single draws and per vertex for batches.
        case Layout::Solid: attribute(1, MTLVertexFormatUChar4Normalized, MtlSlotAttribute, 4, MTLVertexStepFunctionPerInstance); break;
        case Layout::SolidBatch: attribute(1, MTLVertexFormatUChar4Normalized, MtlSlotAttribute, 4, MTLVertexStepFunctionPerVertex); break;
        case Layout::Compose: attribute(3, MTLVertexFormatFloat2, MtlSlotMask, 8, MTLVertexStepFunctionPerVertex);  // fallthrough
        case Layout::Image:
        case Layout::Quad: attribute(1, MTLVertexFormatFloat2, MtlSlotAttribute, 8, MTLVertexStepFunctionPerVertex); break;
        default: break;
    }
    return desc;
}

static void _setBlend(MTLRenderPipelineColorAttachmentDescriptor* color, MTLBlendFactor rgbSrc, MTLBlendFactor rgbDst, MTLBlendFactor alphaDst)
{
    color.blendingEnabled = YES;
    color.rgbBlendOperation = MTLBlendOperationAdd;
    color.alphaBlendOperation = MTLBlendOperationAdd;
    color.sourceRGBBlendFactor = rgbSrc;
    color.destinationRGBBlendFactor = rgbDst;
    color.sourceAlphaBlendFactor = MTLBlendFactorOne;
    color.destinationAlphaBlendFactor = alphaDst;
}

static MTLStencilDescriptor* _stencilDescriptor(const StencilFace& face, uint32_t mask)
{
    auto desc = [[[MTLStencilDescriptor alloc] init] autorelease];
    desc.stencilCompareFunction = face.compare;
    desc.stencilFailureOperation = face.op;
    desc.depthFailureOperation = MTLStencilOperationZero;
    desc.depthStencilPassOperation = face.op;
    desc.readMask = mask;
    desc.writeMask = mask;
    return desc;
}

static id<MTLDepthStencilState> _createDepthStencil(id<MTLDevice> device, const DepthStencil& info)
{
    auto desc = [[MTLDepthStencilDescriptor alloc] init];
    desc.depthCompareFunction = info.depthCompare;
    desc.depthWriteEnabled = info.depthWrite;
    auto mask = (&info == &_dsConv || &info == &_dsClippedStroke) ? 0 : 0xFF;
    desc.frontFaceStencil = _stencilDescriptor(info.front, mask);
    desc.backFaceStencil = _stencilDescriptor(info.back, mask);
    auto state = [device newDepthStencilStateWithDescriptor:desc];
    [desc release];
    return state;
}

static MTLRenderPipelineDescriptor* _pipelineDescriptor(id<MTLFunction> vs, id<MTLFunction> fs, Layout layout, MTLPixelFormat format, bool multisampled)
{
    auto desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.vertexDescriptor = _vertexDescriptor(layout);
    desc.colorAttachments[0].pixelFormat = format;
    desc.colorAttachments[0].writeMask = fs ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
    desc.rasterSampleCount = multisampled ? 4 : 1;
    if (multisampled) {
        desc.depthAttachmentPixelFormat = MTL_DEPTH_STENCIL_FORMAT;
        desc.stencilAttachmentPixelFormat = MTL_DEPTH_STENCIL_FORMAT;
    }
    return desc;
}

static id<MTLRenderPipelineState> _createPipeline(id<MTLDevice> device, MTLRenderPipelineDescriptor* desc)
{
    NSError* error = nil;
    auto state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!state) TVGERR("MTL_ENGINE", "Failed to create a pipeline: %s", error.localizedDescription.UTF8String);
    return state;
}

id<MTLLibrary> MtlPipelines::createLibrary(const char* source)
{
    auto options = [[MTLCompileOptions alloc] init];
    options.fastMathEnabled = NO;  // Shaders rely on IEEE infinities, for example at the radial gradient focal point.
    NSError* error = nil;
    auto library = [context->device newLibraryWithSource:[NSString stringWithUTF8String:source] options:options error:&error];
    if (!library) TVGERR("MTL_ENGINE", "Failed to compile shaders: %s", error.localizedDescription.UTF8String);
    [options release];
    return library;
}

id<MTLLibrary> MtlPipelines::blendLibrary(Material material)
{
    if (!blendLibraries[material]) {
        static const char* sources[]{cMtlShaderSrc_Solid_Blend, cMtlShaderSrc_Linear_Blend, cMtlShaderSrc_Radial_Blend, cMtlShaderSrc_Conic_Blend, cMtlShaderSrc_Image_Blend, cMtlShaderSrc_Scene_Blend};
        char source[32768];
        strcpy(source, context->tiled ? "#define MTL_TILED\n" : "");
        strcat(source, cMtlShaderSrc_Common);
        strcat(source, sources[material]);
        strcat(source, cMtlShaderSrc_BlendFuncs);
        blendLibraries[material] = createLibrary(source);
    }
    return blendLibraries[material];
}

id<MTLFunction> MtlPipelines::function(const char* name)
{
    return [[library newFunctionWithName:[NSString stringWithUTF8String:name]] autorelease];
}

const MtlPipeline& MtlPipelines::get(MtlPipe pipe)
{
    auto& pipeline = pipelines[(uint32_t)pipe];
    if (pipeline.state) return pipeline;

    const auto& info = _pipelineInfos[(uint32_t)pipe];
    auto desc = _pipelineDescriptor(function(info.vs), info.fs ? function(info.fs) : nil, info.layout, MTL_TARGET_FORMAT, info.depthStencil != nullptr);
    if (info.output == Output::Normal) _setBlend(desc.colorAttachments[0], MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorOneMinusSourceAlpha);
    pipeline.state = _createPipeline(context->device, desc);
    if (info.depthStencil) pipeline.depthStencil = _createDepthStencil(context->device, *info.depthStencil);
    return pipeline;
}

const MtlPipeline& MtlPipelines::blendPipeline(Material material, BlendMethod method)
{
    auto& pipeline = blends[material][(uint32_t)method];
    if (pipeline.state) return pipeline;

    static const char* vertexShaders[]{"vs_solid", "vs_gradient", "vs_gradient", "vs_gradient", "vs_image", "vs_quad"};
    static const char* fragmentShaders[]{"fs_solid", "fs_linear", "fs_radial", "fs_conic", "fs_image", "fs_scene"};
    static const Layout layouts[]{Layout::Solid, Layout::Shape, Layout::Shape, Layout::Shape, Layout::Image, Layout::Quad};

    auto name = _blendNames[(uint32_t)method];
    auto fs = name ? [[blendLibrary(material) newFunctionWithName:[NSString stringWithUTF8String:name]] autorelease] : function(fragmentShaders[material]);
    auto desc = _pipelineDescriptor(function(vertexShaders[material]), fs, layouts[material], MTL_TARGET_FORMAT, true);
    auto color = desc.colorAttachments[0];
    switch (method) {
        case BlendMethod::Screen: _setBlend(color, MTLBlendFactorOne, MTLBlendFactorOneMinusSourceColor, MTLBlendFactorOneMinusSourceAlpha); break;
        case BlendMethod::Exclusion: _setBlend(color, MTLBlendFactorOneMinusDestinationColor, MTLBlendFactorOneMinusSourceColor, MTLBlendFactorOneMinusSourceAlpha); break;
        case BlendMethod::Add: _setBlend(color, MTLBlendFactorOne, MTLBlendFactorOne, MTLBlendFactorOne); break;
        default: break;
    }
    pipeline.state = _createPipeline(context->device, desc);
    pipeline.depthStencil = _createDepthStencil(context->device, material == MaterialScene ? _dsScene : _dsShape);
    return pipeline;
}

const MtlPipeline& MtlPipelines::solidBlend(BlendMethod method) { return blendPipeline(MaterialSolid, method); }
const MtlPipeline& MtlPipelines::linearBlend(BlendMethod method) { return blendPipeline(MaterialLinear, method); }
const MtlPipeline& MtlPipelines::radialBlend(BlendMethod method) { return blendPipeline(MaterialRadial, method); }
const MtlPipeline& MtlPipelines::conicBlend(BlendMethod method) { return blendPipeline(MaterialConic, method); }
const MtlPipeline& MtlPipelines::imageBlend(BlendMethod method) { return blendPipeline(MaterialImage, method); }
const MtlPipeline& MtlPipelines::sceneBlend(BlendMethod method) { return blendPipeline(MaterialScene, method); }

const MtlPipeline& MtlPipelines::clippedStroke(MtlRenderSettingsType type)
{
    auto index = uint32_t(type) - 1;
    auto& pipeline = strokeClip[index];
    if (pipeline.state) return pipeline;

    static const char* fragmentShaders[]{"fs_solid", "fs_linear", "fs_radial", "fs_conic"};
    auto solid = type == MtlRenderSettingsType::Solid;
    auto desc = _pipelineDescriptor(function(solid ? "vs_solid" : "vs_gradient"), function(fragmentShaders[index]), solid ? Layout::Solid : Layout::Shape, MTL_TARGET_FORMAT, true);
    _setBlend(desc.colorAttachments[0], MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorOneMinusSourceAlpha);
    pipeline.state = _createPipeline(context->device, desc);
    pipeline.depthStencil = _createDepthStencil(context->device, _dsClippedStroke);
    return pipeline;
}

const MtlPipeline& MtlPipelines::sceneCompose(uint32_t method)
{
    auto& pipeline = compose[method];
    if (pipeline.state) return pipeline;

    auto desc = _pipelineDescriptor(function("vs_compose"), function(_composeNames[method]), Layout::Compose, MTL_TARGET_FORMAT, true);
    if (method < (uint32_t)MaskMethod::Add) _setBlend(desc.colorAttachments[0], MTLBlendFactorOne, MTLBlendFactorOneMinusSourceAlpha, MTLBlendFactorOneMinusSourceAlpha);
    pipeline.state = _createPipeline(context->device, desc);
    pipeline.depthStencil = _createDepthStencil(context->device, _dsScene);
    return pipeline;
}

id<MTLRenderPipelineState> MtlPipelines::blit(MTLPixelFormat format, bool premultiplied)
{
    if (blitFormat != format) {
        [blitPipelines[0] release];
        [blitPipelines[1] release];
        blitPipelines[0] = blitPipelines[1] = nil;
        blitFormat = format;
    }
    auto& pipeline = blitPipelines[premultiplied ? 1 : 0];
    if (!pipeline) {
        auto desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = function("vs_blit");
        desc.fragmentFunction = function(premultiplied ? "fs_blit" : "fs_blit_unpremultiplied");
        desc.colorAttachments[0].pixelFormat = format;
        pipeline = _createPipeline(context->device, desc);
        [desc release];
    }
    return pipeline;
}

void MtlPipelines::initialize(MtlContext& context)
{
    this->context = &context;
    library = createLibrary(cMtlShaderSrc_Main);
}

static void _release(MtlPipeline& pipeline)
{
    [pipeline.state release];
    [pipeline.depthStencil release];
    pipeline = {};
}

void MtlPipelines::release()
{
    for (auto& pipeline : pipelines) _release(pipeline);
    for (auto& material : blends) {
        for (auto& pipeline : material) _release(pipeline);
    }
    for (auto& pipeline : strokeClip) _release(pipeline);
    for (auto& pipeline : compose) _release(pipeline);
    for (auto& pipeline : blitPipelines) {
        [pipeline release];
        pipeline = nil;
    }
    for (auto& blendLibrary : blendLibraries) {
        [blendLibrary release];
        blendLibrary = nil;
    }
    [library release];
    library = nil;
    blitFormat = MTLPixelFormatInvalid;
    context = nullptr;
}
