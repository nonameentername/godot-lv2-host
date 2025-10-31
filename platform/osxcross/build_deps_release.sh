#!/bin/bash

$(eval /osxcross/tools/osxcross_conf.sh)

dir=$(realpath .)

# configure godot-lv2-host

build_dir=$dir/addons/lv2-host/bin/osxcross-$ARCH/release

mkdir -p $build_dir
cd $build_dir

cmake -DCUSTOM_CMAKE=$dir/platform/osxcross/custom-osx.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_VERBOSE_MAKEFILE=1 \
    -DCMAKE_SYSTEM_NAME=Darwin \
    -DOSXCROSS_TARGET_DIR=${OSXCROSS_TARGET_DIR} \
    -DOSXCROSS_SDK=${OSXCROSS_SDK} \
    -DOSXCROSS_TARGET=${OSXCROSS_TARGET} \
    -DCMAKE_OSX_ARCHITECTURES=${ARCH} \
    $dir

#make
#make install

# build godot-lv2-host

#cd $dir/addons/lv2-host/bin/osxcross-$ARCH/release
#make
