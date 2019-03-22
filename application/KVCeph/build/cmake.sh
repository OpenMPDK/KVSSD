#!/bin/bash

rm -rf ceph-runtime
mkdir -p ceph-runtime

cmake -DWITH_TESTS=ON -DWITH_FIO=ON -DFIO_INCLUDE_DIR=../fio -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./ceph-runtime ..
#cmake -DWITH_TESTS=ON -DWITH_FIO=ON -DFIO_INCLUDE_DIR=../fio -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./ceph-runtime ..

