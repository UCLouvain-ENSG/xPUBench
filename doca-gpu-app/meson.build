#
# Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

project('DOCA_SAMPLE', 'C', 'CPP', 'CUDA',
	# Get version number from file.
	version: run_command(find_program('cat'),
		files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
	license: 'Proprietary',
	default_options: ['buildtype=release', 'warning_level=2', 'werror=true'],
	meson_version: '>= 0.61.2'
)

SAMPLE_NAME = 'gpunetio_simple_receive'

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp', 'cuda'])

sample_dependencies = []
sample_dependencies += dependency('doca-common')
doca_gpu_dep = dependency('doca-gpunetio')
sample_dependencies += doca_gpu_dep
sample_dependencies += dependency('doca-argp')
sample_dependencies += dependency('doca-dma')
sample_dependencies += dependency('doca-flow')
sample_dependencies += dependency('doca-eth')
sample_dependencies += dependency('libdpdk')

cuda = import('unstable-cuda')
nvcc = meson.get_compiler('cuda')

nvcc_flags = []
nvcc_flags += ['-gencode', 'arch=compute_70,code=sm_70']
nvcc_flags += ['-gencode', 'arch=compute_80,code=sm_80']
nvcc_flags += ['-gencode', 'arch=compute_90,code=sm_90']
nvcc_flags += ['-Werror', 'all-warnings']
add_project_arguments(nvcc_flags, language: 'cuda')

# Refer to https://mesonbuild.com/Cuda-module.html
add_project_arguments('-forward-unknown-to-host-compiler', language: 'cuda')
add_project_arguments('-rdc=true', language: 'cuda')

gpu_dependencies = []
gpu_dependencies += dependency('cuda', version: '>= 12-1', modules: ['cudart', 'cuda'])

nvcc_flags_link = []
nvcc_flags_link += ['-gencode=arch=compute_70,code=sm_70']
nvcc_flags_link += ['-gencode=arch=compute_80,code=sm_80']
nvcc_flags_link += ['-gencode=arch=compute_90,code=sm_90']
add_project_link_arguments(nvcc_flags_link, language: 'cuda')

sample_dependencies += gpu_dependencies

gpunetio_device_path = doca_gpu_dep.get_variable(pkgconfig : 'libdir')
dependency_gpunetio_device = declare_dependency(compile_args : '-Wl,--whole-archive',
                                                link_args : ['-L' + gpunetio_device_path , '-ldoca_gpunetio_device'],)
sample_dependencies += doca_gpu_dep
sample_dependencies += dependency_gpunetio_device

dependency_dpdk = dependency('libdpdk')
sample_dependencies += dependency_dpdk

sample_srcs = [
	# The sample itself
	'device/' + SAMPLE_NAME + '_kernel.cu',
	'host/' + SAMPLE_NAME + '_sample.c',
	# Main function for the sample's executable
	SAMPLE_NAME + '_main.c',
	'common.c',
]

sample_inc_dirs  = []
sample_inc_dirs += include_directories('.')

executable('doca_' + SAMPLE_NAME, sample_srcs,
	c_args : '-Wno-missing-braces',
	dependencies : sample_dependencies,
	include_directories: sample_inc_dirs,
	install: false)
