source_file = [
   'tvgFlutterLottieAnimation.cpp',
]

thorvg_flutter_dep = declare_dependency(include_directories
                                        : include_directories('.'), sources
                                        : source_file)





shared_library(
    'thorvg',
    [ 'tvgFlutterLottieAnimation.cpp' ],
            include_directories : headers,

    dependencies : [thorvg_lib_dep, thorvg_flutter_dep]
)

library(
    'thorvg',
    [ 'tvgFlutterLottieAnimation.cpp' ],
            include_directories : headers,

    dependencies : [thorvg_lib_dep, thorvg_flutter_dep]
)