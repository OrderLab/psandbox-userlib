#!/bin/bash

mkdir -p build && cd build
cmake ..
make -j $(nproc)
cd ..
cp `pwd`/build/libs/libpsandbox.so `pwd`/../script/libpsandbox_psandbox.so
echo "export PSANDBOXDIR=`pwd`" >> $HOME/.bashrc
echo "export LD_LIBRARY_PATH=`pwd`/build/libs:$LD_LIBRARY_PATH" >> $HOME/.bashrc
source ~/.bashrc
