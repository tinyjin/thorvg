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

#include "tvgVkPipelines.h"

static const VkShaderCode* _shaderCodes[]{
    &cShaderStencilVert, &cShaderDepthVert, &cShaderSolidVert, &cShaderGradientVert, &cShaderImageVert, &cShaderQuadVert, &cShaderComposeVert,
    &cShaderSolidFrag, &cShaderLinearFrag, &cShaderRadialFrag, &cShaderConicFrag, &cShaderImageFrag, &cShaderSceneFrag, &cShaderBlendFrag,
    &cShaderComposeFrag, &cShaderBlitFrag, &cShaderEffectFrag, &cShaderShadowFrag, &cShaderPremultiplyComp};

// Every pass depends on everything that wrote or read its attachments before, and everything after depends on it.
// The engine draws in long passes, so the coarse scopes cost nothing measurable while keeping the passes self-contained.
static const VkSubpassDependency _dependencies[]{
    {VK_SUBPASS_EXTERNAL, 0,
     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
     0},
    {0, VK_SUBPASS_EXTERNAL,
     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
     0},
    {0, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT}};


static VkRenderPass _createPass(VkContext& context, const VkAttachmentDescription* attachments, uint32_t count, bool multisampled, bool depth, const VkSubpassDependency* dependencies)
{
    VkAttachmentReference color{0, multisampled ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolved{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthStencil{multisampled ? 2u : 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.inputAttachmentCount = multisampled ? 1 : 0;
    subpass.pInputAttachments = &color;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pResolveAttachments = multisampled ? &resolved : nullptr;
    subpass.pDepthStencilAttachment = depth ? &depthStencil : nullptr;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = count;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = multisampled ? 3 : 2;
    info.pDependencies = dependencies;
    VkRenderPass pass = VK_NULL_HANDLE;
    context.vkCreateRenderPass(context.device, &info, nullptr, &pass);
    return pass;
}


static VkStencilOpState _stencil(VkCompareOp compare, VkStencilOp op)
{
    return {op, op, VK_STENCIL_OP_ZERO, compare, 0xff, 0xff, 0};
}


static VkPipelineDepthStencilStateCreateInfo _depthStencil(VkCompareOp depthCompare, bool depthWrite, VkCompareOp front, VkStencilOp frontOp, VkCompareOp back, VkStencilOp backOp)
{
    VkPipelineDepthStencilStateCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    info.depthTestEnable = VK_TRUE;
    info.depthWriteEnable = depthWrite;
    info.depthCompareOp = depthCompare;
    info.stencilTestEnable = VK_TRUE;
    info.front = _stencil(front, frontOp);
    info.back = _stencil(back, backOp);
    info.maxDepthBounds = 1.0f;
    return info;
}


static VkPipelineDepthStencilStateCreateInfo _depthStencil(VkCompareOp depthCompare, bool depthWrite, VkCompareOp compare, VkStencilOp op)
{
    return _depthStencil(depthCompare, depthWrite, compare, op, compare, op);
}


static VkPipelineDepthStencilStateCreateInfo _depthOnly(VkCompareOp compare)
{
    VkPipelineDepthStencilStateCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    info.depthTestEnable = VK_TRUE;
    info.depthCompareOp = compare;
    info.maxDepthBounds = 1.0f;
    return info;
}


static StrictKey _registryKey;
static Array<VkPipelines*> _registry;


VkPipelines* VkPipelines::acquire(VkContext& context)
{
    ScopedLock lock(_registryKey);
    for (auto& p : _registry) {
        if (p->device == context.device) {
            ++p->refCnt;
            return p;
        }
    }
    auto pipelines = new VkPipelines;
    if (!pipelines->initialize(context)) {
        pipelines->destroy(context);
        delete(pipelines);
        return nullptr;
    }
    _registry.push(pipelines);
    return pipelines;
}


void VkPipelines::release(VkContext& context, VkPipelines* pipelines)
{
    ScopedLock lock(_registryKey);
    if (--pipelines->refCnt > 0) return;
    for (uint32_t i = 0; i < _registry.count; ++i) {
        if (_registry[i] != pipelines) continue;
        _registry[i] = _registry.last();
        _registry.pop();
        break;
    }
    pipelines->destroy(context);
    delete(pipelines);
}


bool VkPipelines::initialize(VkContext& context)
{
    device = context.device;

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    context.vkCreatePipelineCache(context.device, &cacheInfo, nullptr, &cache);

    // descriptor set layouts
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 2;
        info.pBindings = bindings;
        context.vkCreateDescriptorSetLayout(context.device, &info, nullptr, &layoutUniforms);
    }
    {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 1;
        info.pBindings = &binding;
        context.vkCreateDescriptorSetLayout(context.device, &info, nullptr, &layoutTexture);
    }
    {
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 1;
        info.pBindings = &binding;
        context.vkCreateDescriptorSetLayout(context.device, &info, nullptr, &layoutBackdrop);
    }
    {
        VkDescriptorSetLayoutBinding bindings[2]{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 2;
        info.pBindings = bindings;
        context.vkCreateDescriptorSetLayout(context.device, &info, nullptr, &layoutPrepare);
    }

    // pipeline layouts
    {
        VkDescriptorSetLayout sets[]{layoutUniforms, layoutTexture, layoutTexture, layoutBackdrop};
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 4;
        info.pSetLayouts = sets;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        context.vkCreatePipelineLayout(context.device, &info, nullptr, &pipelineLayout);
    }
    {
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 20};
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &layoutPrepare;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;
        context.vkCreatePipelineLayout(context.device, &info, nullptr, &prepareLayout);
    }

    auto format = VK_FORMAT_R8G8B8A8_UNORM;
    VkAttachmentDescription ms[3]{
        {0, format, VK_SAMPLE_COUNT_4_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL},
        {0, format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {0, context.depthStencilFormat, VK_SAMPLE_COUNT_4_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    passMS = _createPass(context, ms, 3, true, true, _dependencies);

    for (uint32_t load = 0; load < 2; ++load) {
        auto loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentDescription ss{0, format, VK_SAMPLE_COUNT_1_BIT, loadOp, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                   load ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        passSS[load] = _createPass(context, &ss, 1, false, false, _dependencies);
    }
    return layoutBackdrop && pipelineLayout && prepareLayout && passMS && passSS[0] && passSS[1];
}


void VkPipelines::destroy(VkContext& context)
{
    auto destroyPipeline = [&](VkPipeline& pipeline) {
        if (pipeline) context.vkDestroyPipeline(context.device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    };
    for (auto& p : pipes) destroyPipeline(p);
    for (auto& p : strokeClips) destroyPipeline(p);
    for (auto& material : blends) {
        for (auto& p : material) destroyPipeline(p);
    }
    for (auto& p : composes) destroyPipeline(p);
    for (auto& p : effects) destroyPipeline(p);
    for (auto& p : premultiplies) destroyPipeline(p);
    for (auto& p : blits) destroyPipeline(p.pipeline);
    blits.clear();
    for (auto& p : blitPasses) context.vkDestroyRenderPass(context.device, p.pass, nullptr);
    blitPasses.clear();
    if (passMS) context.vkDestroyRenderPass(context.device, passMS, nullptr);
    passMS = VK_NULL_HANDLE;
    for (auto& pass : passSS) {
        if (pass) context.vkDestroyRenderPass(context.device, pass, nullptr);
        pass = VK_NULL_HANDLE;
    }
    for (auto& module : modules) {
        if (module) context.vkDestroyShaderModule(context.device, module, nullptr);
        module = VK_NULL_HANDLE;
    }
    if (prepareLayout) context.vkDestroyPipelineLayout(context.device, prepareLayout, nullptr);
    if (pipelineLayout) context.vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
    if (layoutPrepare) context.vkDestroyDescriptorSetLayout(context.device, layoutPrepare, nullptr);
    if (layoutBackdrop) context.vkDestroyDescriptorSetLayout(context.device, layoutBackdrop, nullptr);
    if (layoutTexture) context.vkDestroyDescriptorSetLayout(context.device, layoutTexture, nullptr);
    if (layoutUniforms) context.vkDestroyDescriptorSetLayout(context.device, layoutUniforms, nullptr);
    if (cache) context.vkDestroyPipelineCache(context.device, cache, nullptr);
}


VkShaderModule VkPipelines::module(VkContext& context, Shader shader)
{
    if (!modules[shader]) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = _shaderCodes[shader]->size;
        info.pCode = _shaderCodes[shader]->code;
        context.vkCreateShaderModule(context.device, &info, nullptr, &modules[shader]);
    }
    return modules[shader];
}


VkPipeline VkPipelines::create(VkContext& context, const Desc& desc)
{
    // vertex input bindings: 0 scene adjustment (per instance), 1 position, 2 color or texture coordinate, 3 mask coordinate
    VkVertexInputBindingDescription bindings[4];
    VkVertexInputAttributeDescription attributes[4];
    uint32_t bindingCount = 0, attributeCount = 0;
    auto bind = [&](uint32_t binding, uint32_t stride, VkVertexInputRate rate, uint32_t location, VkFormat format) {
        bindings[bindingCount++] = {binding, stride, rate};
        attributes[attributeCount++] = {location, binding, format, 0};
    };
    bind(1, sizeof(Point), VK_VERTEX_INPUT_RATE_VERTEX, 0, VK_FORMAT_R32G32_SFLOAT);
    if (desc.input == Input::Solid) bind(2, sizeof(RenderColor), VK_VERTEX_INPUT_RATE_INSTANCE, 1, VK_FORMAT_R8G8B8A8_UNORM);
    else if (desc.input == Input::SolidBatch) bind(2, sizeof(RenderColor), VK_VERTEX_INPUT_RATE_VERTEX, 1, VK_FORMAT_R8G8B8A8_UNORM);
    else if (desc.input != Input::Stencil) bind(2, sizeof(Point), VK_VERTEX_INPUT_RATE_VERTEX, 1, VK_FORMAT_R32G32_SFLOAT);
    if (desc.input == Input::Compose) bind(3, sizeof(Point), VK_VERTEX_INPUT_RATE_VERTEX, 3, VK_FORMAT_R32G32_SFLOAT);
    if (desc.input != Input::Quad && desc.input != Input::Compose) bind(0, 4 * sizeof(float), VK_VERTEX_INPUT_RATE_INSTANCE, 2, VK_FORMAT_R32G32B32A32_SFLOAT);

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = bindingCount;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = attributeCount;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = desc.samples;

    VkPipelineDepthStencilStateCreateInfo depthStencil;
    switch (desc.depth) {
        case Depth::NonZero: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, false, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_DECREMENT_AND_WRAP); break;
        case Depth::EvenOdd: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, false, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_INVERT); break;
        case Depth::Direct: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, false, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE); break;
        case Depth::CopyStencilToDepth: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, true, VK_COMPARE_OP_NOT_EQUAL, VK_STENCIL_OP_ZERO); break;
        case Depth::CopyStencilToDepthInterm: depthStencil = _depthStencil(VK_COMPARE_OP_GREATER, true, VK_COMPARE_OP_NOT_EQUAL, VK_STENCIL_OP_ZERO); break;
        case Depth::CopyDepthToStencil: depthStencil = _depthStencil(VK_COMPARE_OP_EQUAL, false, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_REPLACE); break;
        case Depth::MergeDepthStencil: depthStencil = _depthStencil(VK_COMPARE_OP_EQUAL, true, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_KEEP); break;
        case Depth::ClearDepth: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, true, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_KEEP); break;
        case Depth::Shape: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, false, VK_COMPARE_OP_NOT_EQUAL, VK_STENCIL_OP_ZERO); break;
        case Depth::Scene: depthStencil = _depthStencil(VK_COMPARE_OP_ALWAYS, false, VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_ZERO); break;
        case Depth::Conv: depthStencil = _depthOnly(VK_COMPARE_OP_ALWAYS); break;
        case Depth::ClipEqual: depthStencil = _depthOnly(VK_COMPARE_OP_EQUAL); break;
        default: break;
    }

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = desc.blend != Blend::None && desc.blend != Blend::Src;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (desc.blend == Blend::Screen || desc.blend == Blend::Exclusion) {
        if (desc.blend == Blend::Exclusion) attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    } else if (desc.blend == Blend::Add) {
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    attachment.colorWriteMask = desc.blend == Blend::None ? 0 : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = desc.depth == Depth::None ? 2 : 3;
    dynamic.pDynamicStates = dynamics;

    VkSpecializationMapEntry entries[2]{{0, 0, sizeof(int32_t)}, {1, sizeof(int32_t), sizeof(int32_t)}};
    VkSpecializationInfo specialization{desc.specCount, entries, desc.specCount * sizeof(int32_t), desc.spec};

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = module(context, desc.vs);
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = desc.specCount ? &specialization : nullptr;
    if (desc.fs != ShaderCount) stages[1].module = module(context, desc.fs);

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = desc.fs == ShaderCount ? 1 : 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterization;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = desc.depth == Depth::None ? nullptr : &depthStencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;
    info.renderPass = desc.pass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (context.vkCreateGraphicsPipelines(context.device, cache, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        TVGERR("VK_ENGINE", "Failed to create a graphics pipeline.");
        return VK_NULL_HANDLE;
    }
    return pipeline;
}


VkPipelines::Desc VkPipelines::desc(VkPipe pipe)
{
    Desc d{StencilVert, ShaderCount, Input::Stencil, Depth::None, Blend::None, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0};
    switch (pipe) {
        case VkPipe::NonZero: d.depth = Depth::NonZero; break;
        case VkPipe::EvenOdd: d.depth = Depth::EvenOdd; break;
        case VkPipe::Direct: d.depth = Depth::Direct; break;
        case VkPipe::CopyStencilToDepth: d.vs = DepthVert; d.depth = Depth::CopyStencilToDepth; break;
        case VkPipe::CopyStencilToDepthInterm: d.vs = DepthVert; d.depth = Depth::CopyStencilToDepthInterm; break;
        case VkPipe::CopyDepthToStencil: d.vs = DepthVert; d.depth = Depth::CopyDepthToStencil; break;
        case VkPipe::MergeDepthStencil: d.vs = DepthVert; d.depth = Depth::MergeDepthStencil; break;
        case VkPipe::ClearDepth: d.vs = DepthVert; d.depth = Depth::ClearDepth; break;
        case VkPipe::Solid: d = {SolidVert, SolidFrag, Input::Solid, Depth::Shape, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::SolidConv: d = {SolidVert, SolidFrag, Input::Solid, Depth::Conv, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::SolidBatch: d = {SolidVert, SolidFrag, Input::SolidBatch, Depth::Scene, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::SolidStencilBatch: d = {SolidVert, SolidFrag, Input::SolidBatch, Depth::Shape, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Linear: d = {GradientVert, LinearFrag, Input::Stencil, Depth::Shape, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Radial: d = {GradientVert, RadialFrag, Input::Stencil, Depth::Shape, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Conic: d = {GradientVert, ConicFrag, Input::Stencil, Depth::Shape, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::LinearConv: d = {GradientVert, LinearFrag, Input::Stencil, Depth::Conv, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::RadialConv: d = {GradientVert, RadialFrag, Input::Stencil, Depth::Conv, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::ConicConv: d = {GradientVert, ConicFrag, Input::Stencil, Depth::Conv, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Image: d = {ImageVert, ImageFrag, Input::Image, Depth::Shape, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::ImageDirect: d = {ImageVert, ImageFrag, Input::Image, Depth::Scene, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Scene: d = {QuadVert, SceneFrag, Input::Quad, Depth::Scene, Blend::Normal, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Restore: d = {QuadVert, BlitFrag, Input::Quad, Depth::Conv, Blend::Src, passMS, VK_SAMPLE_COUNT_4_BIT, {}, 0}; break;
        case VkPipe::Shadow: d = {QuadVert, ShadowFrag, Input::Quad, Depth::None, Blend::Src, passSS[1], VK_SAMPLE_COUNT_1_BIT, {}, 0}; break;
        default: break;
    }
    return d;
}


VkPipeline VkPipelines::get(VkContext& context, VkPipe pipe)
{
    ScopedLock lock(key);
    auto& pipeline = pipes[(int)pipe];
    if (!pipeline) pipeline = create(context, desc(pipe));
    return pipeline;
}


VkPipeline VkPipelines::clippedStroke(VkContext& context, VkRenderSettingsType type)
{
    ScopedLock lock(key);
    auto index = int(type) - 1;
    auto& pipeline = strokeClips[index];
    if (!pipeline) {
        static const VkPipe pipes[]{VkPipe::Solid, VkPipe::Linear, VkPipe::Radial, VkPipe::Conic};
        auto d = desc(pipes[index]);
        d.depth = Depth::ClipEqual;
        pipeline = create(context, d);
    }
    return pipeline;
}


VkPipeline VkPipelines::blend(VkContext& context, VkMaterial material, BlendMethod method)
{
    ScopedLock lock(key);
    auto& pipeline = blends[(int)material][(int)method];
    if (pipeline) return pipeline;

    static const VkPipe normals[]{VkPipe::Solid, VkPipe::Linear, VkPipe::Radial, VkPipe::Conic, VkPipe::Image, VkPipe::Scene};
    auto d = desc(normals[(int)material]);
    d.depth = material == VkMaterial::Scene ? Depth::Scene : Depth::Shape;
    switch (method) {
        case BlendMethod::Screen: d.blend = Blend::Screen; break;
        case BlendMethod::Exclusion: d.blend = Blend::Exclusion; break;
        case BlendMethod::Add: d.blend = Blend::Add; break;
        default:
            // the custom shader outputs the blended color itself
            d.fs = BlendFrag;
            d.blend = Blend::Src;
            d.spec[0] = int32_t(method);
            d.spec[1] = int32_t(material);
            d.specCount = 2;
            break;
    }
    pipeline = create(context, d);
    return pipeline;
}


VkPipeline VkPipelines::compose(VkContext& context, MaskMethod method)
{
    ScopedLock lock(key);
    auto& pipeline = composes[(int)method];
    if (!pipeline) {
        auto blend = method <= MaskMethod::InvLuma ? Blend::Normal : Blend::Src;
        pipeline = create(context, {ComposeVert, ComposeFrag, Input::Compose, Depth::Scene, blend, passMS, VK_SAMPLE_COUNT_4_BIT, {int32_t(method)}, 1});
    }
    return pipeline;
}


VkPipeline VkPipelines::effect(VkContext& context, VkEffect effect)
{
    ScopedLock lock(key);
    auto& pipeline = effects[(int)effect];
    if (!pipeline) pipeline = create(context, {QuadVert, EffectFrag, Input::Quad, Depth::None, Blend::Src, passSS[1], VK_SAMPLE_COUNT_1_BIT, {int32_t(effect)}, 1});
    return pipeline;
}


VkRenderPass VkPipelines::pass(VkContext& context, VkFormat format, bool present)
{
    for (auto& p : blitPasses) {
        if (p.format == format && p.present == present) return p.pass;
    }
    // the blit covers the whole target, its previous contents are never needed
    VkAttachmentDescription attachment{0, format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                       VK_IMAGE_LAYOUT_UNDEFINED, present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // An image target becomes visible to any command the application submits afterwards to the same queue.
    // Both variants share the dependencies, so a pipeline is compatible with either of them.
    const VkSubpassDependency dependencies[]{
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, 0}};
    auto renderPass = _createPass(context, &attachment, 1, false, false, dependencies);
    if (renderPass) blitPasses.push({format, present, renderPass});
    return renderPass;
}


VkRenderPass VkPipelines::blitPass(VkContext& context, VkFormat format, bool present)
{
    ScopedLock lock(key);
    return pass(context, format, present);
}


VkPipeline VkPipelines::blit(VkContext& context, VkFormat format, bool premultiplied)
{
    ScopedLock lock(key);
    for (auto& p : blits) {
        if (p.format == format && p.premultiplied == premultiplied) return p.pipeline;
    }
    // render pass compatibility ignores the final layout, the only difference of the two variants
    auto pipeline = create(context, {QuadVert, BlitFrag, Input::Quad, Depth::None, Blend::Src, pass(context, format, false), VK_SAMPLE_COUNT_1_BIT, {int32_t(!premultiplied)}, 1});
    if (pipeline) blits.push({format, premultiplied, pipeline});
    return pipeline;
}


VkPipeline VkPipelines::premultiply(VkContext& context, bool bgr)
{
    ScopedLock lock(key);
    auto& pipeline = premultiplies[bgr];
    if (pipeline) return pipeline;

    VkSpecializationMapEntry entry{0, 0, sizeof(VkBool32)};
    VkBool32 value = bgr;
    VkSpecializationInfo specialization{1, &entry, sizeof(value), &value};

    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module(context, PremultiplyComp);
    info.stage.pName = "main";
    info.stage.pSpecializationInfo = &specialization;
    info.layout = prepareLayout;
    context.vkCreateComputePipelines(context.device, cache, 1, &info, nullptr, &pipeline);
    return pipeline;
}
