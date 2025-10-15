FastClick-GPU
=========

It extends FastClick by adding GPU-enabled elements.
To make it work, you need to have CUDA installed to your sytem.
You can then use the `--enable-cuda` option at compilation time.
Beware to also enable `--enable-dpdk-packet`, so that the "Packet" class is a wrapper to a DPDK mbuf.

GPU elements are located under `elements/cuda`. The CUDA kernel are under `lib/cuda` (CUDA code) and `include/click/cuda` (header files). Examples of pipelines using these elements are under `conf/cuda`.

