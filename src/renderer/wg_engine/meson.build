source_file = [
    'tvgWgBindGroups.h',
    'tvgWgCommon.h',
    'tvgWgGeometry.h',
    'tvgWgPipelines.h',
    'tvgWgRenderData.h',
    'tvgWgRenderer.h',
    'tvgWgRenderTarget.h',
    'tvgWgShaderSrc.h',
    'tvgWgShaderTypes.h',
    'tvgWgBindGroups.cpp',
    'tvgWgCommon.cpp',
    'tvgWgGeometry.cpp',
    'tvgWgPipelines.cpp',
    'tvgWgRenderData.cpp',
    'tvgWgRenderer.cpp',
    'tvgWgRenderTarget.cpp',
    'tvgWgShaderSrc.cpp',
    'tvgWgShaderTypes.cpp'
]

wgpu_dep = dependency('wgpu_native')

engine_dep += [declare_dependency(
    dependencies        : wgpu_dep,
    include_directories : include_directories('.'),
    sources : source_file
)]
