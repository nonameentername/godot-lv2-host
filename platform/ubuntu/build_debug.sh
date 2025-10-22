#!/bin/bash

echo TAG_VERSION=$TAG_VERSION
echo BUILD_SHA=$BUILD_SHA

dir=$(realpath .)

# configure godot-lilv

build_dir=$dir/addons/lilv/bin/linux/debug

mkdir -p $build_dir
cd $build_dir

cmake -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_VERBOSE_MAKEFILE=1 \
    $dir

# build godot-lilv

#cd $dir/addons/lilv/bin/linux/debug
#make

# build godot-lilv (gdextension)

cd $dir
scons platform=linux target=template_debug dev_build=yes debug_symbols=yes
