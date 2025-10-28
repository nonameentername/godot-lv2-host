#!/bin/bash

echo TAG_VERSION=$TAG_VERSION
echo BUILD_SHA=$BUILD_SHA

dir=$(realpath .)

# configure godot-lv2-host

build_dir=$dir/addons/lv2-host/bin/linux/debug

mkdir -p $build_dir
cd $build_dir

cmake -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=1 \
    $dir
    #-DENABLE_ASAN=ON \

# build godot-lv2-host

#cd $dir/addons/lv2/bin/linux/debug
#make

# build godot-lv2-host (gdextension)

cd $dir
scons platform=linux target=template_debug dev_build=yes debug_symbols=yes #asan=true
