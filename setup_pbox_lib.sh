#!/bin/bash

sudo apt-get install -y cmake
mkdir -p build && cd build
cmake ..
make -j $(nproc)
