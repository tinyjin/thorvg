#!/bin/bash

if [ ! -d "./build_flutter_aarch64" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_aarch64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="lottie, png, jpg" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_aarch64
fi

ninja -C build_flutter_aarch64/

if [ ! -d "./build_flutter_aarch64_iossim" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_simulator_aarch64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="lottie, png, jpg" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_aarch64_iossim
fi

ninja -C build_flutter_aarch64_iossim/

if [ ! -d "./build_flutter_x86_64" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_simulator_x86_64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="lottie, png, jpg" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_x86_64
fi

ninja -C build_flutter_x86_64/

mkdir build_flutter_ios
cp build_flutter_aarch64/src/bindings/flutter/libthorvg.dylib ./build_flutter_ios

lipo build_flutter_aarch64_iossim/src/bindings/flutter/libthorvg.dylib \
build_flutter_x86_64/src/bindings/flutter/libthorvg.dylib \
-output build_flutter_ios/libsimthorvg.dylib -create

ls -lrt build_flutter_ios/libthorvg.dylib
ls -lrt build_flutter_ios/libsimthorvg.dylib
