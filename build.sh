#!/bin/bash

mkdir -p build
pushd build/
#cmake ..
cmake -DTRACING=ON ..
make -j$(nproc)
popd
