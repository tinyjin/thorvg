source_file = [
    'tvgMtlCommon.h',
    'tvgMtlCompositor.h',
    'tvgMtlMesh.h',
    'tvgMtlPipelines.h',
    'tvgMtlRenderData.h',
    'tvgMtlRenderTarget.h',
    'tvgMtlRenderTask.h',
    'tvgMtlRenderer.h',
    'tvgMtlRendererImpl.h',
    'tvgMtlShaderSrc.h',
    'tvgMtlShaderTypes.h',
    'tvgMtlSolidBatch.h',
    'tvgMtlStencilBatch.h',
    'tvgMtlTessellator.h',
    'tvgMtlTextureMgr.h',
    'tvgMtlCommon.mm',
    'tvgMtlCompositor.mm',
    'tvgMtlPipelines.mm',
    'tvgMtlRenderData.mm',
    'tvgMtlRenderTarget.mm',
    'tvgMtlRenderTask.mm',
    'tvgMtlRenderer.mm',
    'tvgMtlSolidBatch.mm',
    'tvgMtlStencilBatch.mm',
    'tvgMtlTextureMgr.mm',
    'tvgMtlMesh.cpp',
    'tvgMtlShaderSrc.cpp',
    'tvgMtlShaderTypes.cpp',
    'tvgMtlTessellator.cpp'
]

add_languages('objcpp', native: false, required: true)

metal_dep = dependency('appleframeworks', modules : ['Foundation', 'Metal', 'QuartzCore'])

engine_dep += [declare_dependency(
    dependencies        : metal_dep,
    include_directories : include_directories('.'),
    sources             : source_file
)]
