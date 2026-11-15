source_file = [
    'tvgVkCommon.h',
    'tvgVkCompositor.h',
    'tvgVkMesh.h',
    'tvgVkPipelines.h',
    'tvgVkRenderData.h',
    'tvgVkRenderer.h',
    'tvgVkRenderTarget.h',
    'tvgVkRenderTask.h',
    'tvgVkShaderSrc.h',
    'tvgVkShaderTypes.h',
    'tvgVkSolidBatch.h',
    'tvgVkStencilBatch.h',
    'tvgVkTextureMgr.h',
    'tvgVkTessellator.h',
    'tvgVkCommon.cpp',
    'tvgVkCompositor.cpp',
    'tvgVkMesh.cpp',
    'tvgVkPipelines.cpp',
    'tvgVkRenderData.cpp',
    'tvgVkRenderer.cpp',
    'tvgVkRenderTarget.cpp',
    'tvgVkRenderTask.cpp',
    'tvgVkShaderSrc.cpp',
    'tvgVkShaderTypes.cpp',
    'tvgVkSolidBatch.cpp',
    'tvgVkStencilBatch.cpp',
    'tvgVkTextureMgr.cpp',
    'tvgVkTessellator.cpp'
]

# headers only: the engine never links the Vulkan loader
vk_dep = vk_headers.found() ? vk_headers.partial_dependency(compile_args: true, includes: true) : []

engine_dep += [declare_dependency(
    dependencies        : vk_dep,
    include_directories : include_directories('.'),
    sources             : source_file
)]
