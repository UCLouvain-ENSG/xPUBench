export LD_LIBRARY_PATH=/opt/mellanox/doca/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig:/opt/mellanox/flexio/lib/pkgconfig:/opt/mellanox/grpc/lib/pkgconfig:$PKG_CONFIG_PATH

export LD_LIBRARY_PATH=/opt/mellanox/dpdk/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/opt/mellanox/dpdk/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH


export LD_LIBRARY_PATH=/usr/local/cuda/lib64:${LD_LIBRARY_PATH}}
export PATH=/usr/local/cuda/bin/:$PATH

sudo modprobe nvidia-peermem
