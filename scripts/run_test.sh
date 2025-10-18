#!/bin/bash

dir=$(realpath .)

$dir/test.sh > $dir/output.txt 2>&1
