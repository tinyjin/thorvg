if (cc.get_id() == 'emscripten')

    if get_option('bindings').contains('wasm_beta') == true
        source_file = files('tvgWgWasmLottieAnimation.cpp')
    else
        source_file = files('tvgWasmLottieAnimation.cpp')
    endif

    thorvg_wasm_dep = declare_dependency(include_directories
                                         : include_directories('.'), sources
                                         : source_file)
    executable('thorvg-wasm',
        [],
        include_directories : headers,
        dependencies : [thorvg_lib_dep, thorvg_wasm_dep],
        )
else
    message('wasm compiler is not the emscripten!')
endif
