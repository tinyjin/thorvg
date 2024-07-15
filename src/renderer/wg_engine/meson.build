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

# link emscripten webgpu.h
config_h = configuration_data()
config_h.set10('THORVG_WG_RASTER_SUPPORT', true)

# wgpu_dep = cc.find_library('webgpu', required: true)
# wgpu_dep = dependency('webgpu', required: false)



engine_dep += [declare_dependency(
    include_directories : include_directories('.'),
    sources : source_file
)]
