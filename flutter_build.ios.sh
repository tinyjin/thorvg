#!/bin/bash

if [ ! -d "./build_flutter_aarch64" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_aarch64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="lottie, png, jpg" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_aarch64
fi

ninja -C build_flutter_aarch64/

if [ ! -d "./build_flutter_x86_64" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_x86_64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="lottie, png, jpg" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_x86_64
fi

ninja -C build_flutter_x86_64/

mkdir build_flutter_ios

# Legacy fat binary (aarch64 simulator not supported)
lipo build_flutter_x86_64/src/bindings/flutter/libthorvg.dylib \
build_flutter_aarch64/src/bindings/flutter/libthorvg.dylib \
-output build_flutter_ios/libthorvg.dylib -create

ls -lrt build_flutter_ios/libthorvg.dylib
