#!/bin/bash

source local.sh

if [ -d "build_$HOSTNAME" ]; then
    mesoncommand="meson build_$HOSTNAME --wipe"
else
    mesoncommand="meson build_$HOSTNAME"
fi

PKG_CONFIG_PATH=/usr/lib/pkgconfig:/opt/mellanox/grpc/lib/x86_64-linux-gnu/pkgconfig:/opt/mellanox/dpdk/lib/x86_64-linux-gnu/pkgconfig:/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH} ${mesoncommand} && ninja -C build_$HOSTNAME

sudo CUDA_PATH_L=/usr/lib/x86_64-linux-gnu LD_LIBRARY_PATH=$LD_LIBRARY_PATH GDRCOPY_PATH_L=/opt/mellanox/gdrcopy/src build/doca_gpunetio_simple_receive -n c1:00.0 -g a1:00.0 -q 8 -w 0 -b 1024
