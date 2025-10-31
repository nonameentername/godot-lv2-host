#!/bin/bash

$(eval /osxcross/tools/osxcross_conf.sh)

dir=$(realpath .)

$dir/scripts/lipo-dir.py  \
    $dir/addons/lv2-host/bin/osxcross-arm64/debug \
    $dir/addons/lv2-host/bin/osxcross-x86_64/debug \
    $dir/addons/lv2-host/bin/osxcross/debug

prefix=$dir/addons/lv2-host/bin/macos/debug
prefix_x64=$dir/addons/lv2-host/bin/osxcross-x86_64/debug
prefix_arm64=$dir/addons/lv2-host/bin/osxcross-arm64/debug

$dir/scripts/lipo-dir.py $prefix_arm64 $prefix_x64 $prefix

export OSXCROSS_ROOT=$OSXCROSS_BASE_DIR

cd $dir
scons platform=macos target=template_debug dev_build=yes debug_symbols=yes osxcross_sdk=$OSXCROSS_TARGET
