#!/bin/bash
# 
# dvblast++ build script
# Copyright (C) 2016, Kylone
# Authors: Gokhan Poyraz <gokhan@kylone.com>
#

pw=`pwd`
rm -rf build
mkdir build
cd build

cmake -DCMAKE_TOOLCHAIN_FILE=/opt/devel/projects/cLsuite/src/scripts/toolchain_x86.cmake -DCMAKE_BUILD_TYPE=Release "${pw}"
# cmake -DCMAKE_BUILD_TYPE=Release "${pw}"
ex="${?}"
if [ "${ex}" == "0" ]; then
   make
   ex="${?}"
fi

cd "${pw}"
if [ "${ex}" == "0" ]; then
  ls -l build/dvblastpp
fi

