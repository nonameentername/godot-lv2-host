#!/bin/bash

echo TAG_VERSION=$TAG_VERSION
echo BUILD_SHA=$BUILD_SHA

dir=$(realpath .)

# configure godot-lv2-host

build_dir=$dir/addons/lv2-host/bin/windows/debug

mkdir -p $build_dir
cd $build_dir

cmake -DCMAKE_TOOLCHAIN_FILE=$dir/modules/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$dir/modules/vcpkg/scripts/toolchains/mingw.cmake \
    -DCMAKE_MAKE_PROGRAM=gmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=1 \
    -DVCPKG_CMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_NAME=MinGW \
    -DVCPKG_TARGET_ARCHITECTURE=x64 \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
    -DVCPKG_DEFAULT_TRIPLET=x64-mingw-static \
    -DCMAKE_CXX_COMPILER=/usr/bin/x86_64-w64-mingw32-g++ \
    -DCMAKE_C_COMPILER=/usr/bin/x86_64-w64-mingw32-gcc \
    -DCMAKE_SYSROOT=/usr/x86_64-w64-mingw32 \
    $dir

# build godot-lv2-host

#cd $dir/addons/lv2/bin/windows/debug
#make

# build godot-lv2-host (gdextension)

#TODO: is this needed?
#cp /usr/lib/gcc/x86_64-w64-mingw32/*-posix/libstdc++-6.dll $build_dir/lib/
#cp /usr/lib/gcc/x86_64-w64-mingw32/*-posix/libgcc_s_seh-1.dll $build_dir/lib/
#cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll $build_dir/lib/

cd $dir
scons platform=windows target=template_debug dev_build=yes debug_symbols=yes

#TODO: is this needed?
#for dll in $(find $build_dir/lib -type f); do
#    cp $dll $build_dir/..
#done
