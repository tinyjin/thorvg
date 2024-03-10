#!/bin/bash

if [ ! -d "./build_flutter_aarch64" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_aarch64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="all" -Dsavers="all" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_aarch64
fi

ninja -C build_flutter_aarch64/

if [ ! -d "./build_flutter_x86_64" ]; then
    sed "s|EMSDK:|$1|g" ./cross/flutter_ios_x86_64.txt > /tmp/.flutter_cross.txt
    meson -Db_lto=true -Ddefault_library=static -Dloaders="all" -Dsavers="all" -Dbindings="flutter_beta" --cross-file /tmp/.flutter_cross.txt build_flutter_x86_64
fi

ninja -C build_flutter_x86_64/

ls -lrt build_flutter_aarch64/src/bindings/flutter/libthorvg.dylib
ls -lrt build_flutter_x86_64/src/bindings/flutter/libthorvg.dylib
