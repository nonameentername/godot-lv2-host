#!/bin/bash

dir=$(realpath .)

for plugin in $(lv2ls); do
    echo ""
    echo "Executing: lilv-host $plugin"
    $dir/../build/bin/lilv-host $plugin

    if [ $? -eq 0 ]; then
        echo "worked $plugin"
    else
        echo "didn't work $plugin"
    fi
done
