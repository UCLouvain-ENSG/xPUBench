/*
 * Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#include <doca_gpunetio_dev_buf.cuh>
#include <doca_gpunetio_dev_eth_txq.cuh>
#include <doca_gpunetio_dev_eth_rxq.cuh>
#include <doca_log.h>

#include "gpunetio_common.h"

DOCA_LOG_REGISTER(GPU_SEND_WAIT_TIME::KERNEL);

#define DOCA_GPUNETIO_DEVICE_GET_TIME(globaltimer) asm volatile("mov.u64 %0, %globaltimer;" : "=l"(globaltimer))

__device__ int raw_to_udp(const uintptr_t buf_addr, struct eth_ip_udp_hdr **hdr, uint8_t **payload)
{
	(*hdr) = (struct eth_ip_udp_hdr *) buf_addr;
	(*payload) = (uint8_t *) (buf_addr + sizeof(struct eth_ip_udp_hdr));

	return 0;
}

__device__ bool matches_prefix(uint32_t addr1, uint32_t addr2, uint32_t mask)
{
    return ((addr1 ^ addr2) & mask) == 0;
}

__device__ uint32_t lookup_entry(uint32_t a, ip_vector_t *ip_list, uint32_t len) 
{
    uint64_t found = 0;
    for (int i = 0; i < len - 1; i++) {
        ip_vector_t r = ip_list[i];
	    bool b = matches_prefix(a, r.addr, r.mask);
        if (b) found = i;
	}
    return found;
}

__device__ uint32_t lookup_route(uint32_t a, ip_vector_t *ip_list, uint32_t len) 
{
    int ei = lookup_entry(a, ip_list, len);

    if (ei >= 0) {
	return ip_list[ei].port;
    } else
	return 0;
}

__global__ void receive_packets(struct doca_gpu_eth_rxq *eth_rxq_gpu0, struct doca_gpu_eth_rxq *eth_rxq_gpu1, struct doca_gpu_eth_rxq *eth_rxq_gpu2, struct doca_gpu_eth_rxq *eth_rxq_gpu3,
								struct doca_gpu_eth_rxq *eth_rxq_gpu4, struct doca_gpu_eth_rxq *eth_rxq_gpu5, struct doca_gpu_eth_rxq *eth_rxq_gpu6, struct doca_gpu_eth_rxq *eth_rxq_gpu7,
								struct doca_gpu_eth_rxq *eth_rxq_gpu8, struct doca_gpu_eth_rxq *eth_rxq_gpu9, struct doca_gpu_eth_rxq *eth_rxq_gpu10, struct doca_gpu_eth_rxq *eth_rxq_gpu11,
								struct doca_gpu_eth_rxq *eth_rxq_gpu12, struct doca_gpu_eth_rxq *eth_rxq_gpu13, struct doca_gpu_eth_rxq *eth_rxq_gpu14, struct doca_gpu_eth_rxq *eth_rxq_gpu15,
								uint32_t *exit_cond, 
								struct doca_gpu_eth_txq *txq0, struct doca_gpu_eth_txq *txq1, struct doca_gpu_eth_txq *txq2, struct doca_gpu_eth_txq *txq3,
								struct doca_gpu_eth_txq *txq4, struct doca_gpu_eth_txq *txq5, struct doca_gpu_eth_txq *txq6, struct doca_gpu_eth_txq *txq7,
								struct doca_gpu_eth_txq *txq8, struct doca_gpu_eth_txq *txq9, struct doca_gpu_eth_txq *txq10, struct doca_gpu_eth_txq *txq11,
								struct doca_gpu_eth_txq *txq12, struct doca_gpu_eth_txq *txq13, struct doca_gpu_eth_txq *txq14, struct doca_gpu_eth_txq *txq15,
								int numq,
								uint8_t workload,
								ip_vector_t* _ip_list_gpu, uint32_t _ip_list_len,
								uint32_t *crc_table,
								uint32_t max_rx_num)
{
	__shared__ uint32_t rx_pkt_num;
	__shared__ uint64_t rx_buf_idx;
	__shared__ uint32_t crc_table_sh[256];

	doca_error_t ret;
	struct doca_gpu_eth_rxq *rxq = NULL;
	struct doca_gpu_eth_txq *txq = NULL;
	struct doca_gpu_buf *buf_ptr = NULL;
	uintptr_t buf_addr;
	uint64_t buf_idx;
	uint16_t nbytes;
	uint8_t tmp[6];
	uint32_t curr_position;
	uint32_t mask_max_position;
	struct eth_ip_udp_hdr *hdr;
	uint8_t *payload;
	uint16_t i,j, size;
	// unsigned long long crc_start = 0, crc_end = 0;

	if (workload == 0xFF)
		return;

	// Select rxq/txq based on blockIdx.x (supports up to 16 queues)
	if (blockIdx.x == 0) {
		rxq = eth_rxq_gpu0;
		txq = txq0;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 1) {
		rxq = eth_rxq_gpu1;
		txq = txq1;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 2) {
		rxq = eth_rxq_gpu2;
		txq = txq2;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 3) {
		rxq = eth_rxq_gpu3;
		txq = txq3;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 4) {
		rxq = eth_rxq_gpu4;
		txq = txq4;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 5) {
		rxq = eth_rxq_gpu5;
		txq = txq5;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 6) {
		rxq = eth_rxq_gpu6;
		txq = txq6;
		if (threadIdx.x == 0 && crc_table) {
			for (i=0; i<256; i++)
				crc_table_sh[i] = crc_table[i];
		}
	} else if (blockIdx.x == 7) {
		rxq = eth_rxq_gpu7;
		txq = txq7;
	} else if (blockIdx.x == 8) {
		rxq = eth_rxq_gpu8;
		txq = txq8;
	} else if (blockIdx.x == 9) {
		rxq = eth_rxq_gpu9;
		txq = txq9;
	} else if (blockIdx.x == 10) {
		rxq = eth_rxq_gpu10;
		txq = txq10;
	} else if (blockIdx.x == 11) {
		rxq = eth_rxq_gpu11;
		txq = txq11;
	} else if (blockIdx.x == 12) {
		rxq = eth_rxq_gpu12;
		txq = txq12;
	} else if (blockIdx.x == 13) {
		rxq = eth_rxq_gpu13;
		txq = txq13;
	} else if (blockIdx.x == 14) {
		rxq = eth_rxq_gpu14;
		txq = txq14;
	} else if (blockIdx.x == 15) {
		rxq = eth_rxq_gpu15;
		txq = txq15;
	} else
		return;
	
	// copy crc table once per block into shared memory
	if (threadIdx.x == 0 && crc_table) {
		for (i=0; i<256; i++)
			crc_table_sh[i] = crc_table[i];
	}
	__syncthreads();

	__threadfence();
	doca_gpu_dev_eth_txq_get_info(txq, &curr_position, &mask_max_position);

	while (DOCA_GPUNETIO_VOLATILE(*exit_cond) == 0) {
		ret = doca_gpu_dev_eth_rxq_receive_block(rxq, max_rx_num, MAX_RX_TIMEOUT_NS, &rx_pkt_num, &rx_buf_idx);
		/* If any thread returns receive error, the whole execution stops */
		if (ret != DOCA_SUCCESS) {
			if (threadIdx.x == 0) {
				/*
				 * printf in CUDA kernel may be a good idea only to report critical errors or debugging.
				 * If application prints this message on the console, something bad happened and
				 * applications needs to exit
				 */
				printf("Receive UDP kernel error %d rxpkts %d error %d\n", ret, rx_pkt_num, ret);
				DOCA_GPUNETIO_VOLATILE(*exit_cond) = 1;
			}

			break;
		}

		if (rx_pkt_num == 0)
			continue;

		//printf("Received RX %d\n", rx_pkt_num);
		buf_idx = threadIdx.x;
		while (buf_idx < rx_pkt_num) {
			ret = doca_gpu_dev_eth_rxq_get_buf(rxq, rx_buf_idx + buf_idx, &buf_ptr);
			if (ret != DOCA_SUCCESS) {
				printf("UDP Error %d doca_gpu_dev_eth_rxq_get_buf thread %d\n", ret, threadIdx.x);
				DOCA_GPUNETIO_VOLATILE(*exit_cond) = 1;
				break;
			}

			ret = doca_gpu_dev_buf_get_addr(buf_ptr, &buf_addr);
			if (ret != DOCA_SUCCESS) {
				printf("UDP Error %d doca_gpu_dev_eth_rxq_get_buf thread %d\n", ret, threadIdx.x);
				DOCA_GPUNETIO_VOLATILE(*exit_cond) = 1;
				break;
			}

			raw_to_udp(buf_addr, &hdr, &payload);

			nbytes = BE_TO_CPU_16(hdr->l3_hdr.total_length);
			tmp[0] = hdr->l2_hdr.s_addr_bytes[0];
			tmp[1] = hdr->l2_hdr.s_addr_bytes[1];
			tmp[2] = hdr->l2_hdr.s_addr_bytes[2];
			tmp[3] = hdr->l2_hdr.s_addr_bytes[3];
			tmp[4] = hdr->l2_hdr.s_addr_bytes[4];
			tmp[5] = hdr->l2_hdr.s_addr_bytes[5];

			hdr->l2_hdr.s_addr_bytes[0] = hdr->l2_hdr.d_addr_bytes[0];
			hdr->l2_hdr.s_addr_bytes[1] = hdr->l2_hdr.d_addr_bytes[1];
			hdr->l2_hdr.s_addr_bytes[2] = hdr->l2_hdr.d_addr_bytes[2];
			hdr->l2_hdr.s_addr_bytes[3] = hdr->l2_hdr.d_addr_bytes[3];
			hdr->l2_hdr.s_addr_bytes[4] = hdr->l2_hdr.d_addr_bytes[4];
			hdr->l2_hdr.s_addr_bytes[5] = hdr->l2_hdr.d_addr_bytes[5];

			hdr->l2_hdr.d_addr_bytes[0] = tmp[0];
			hdr->l2_hdr.d_addr_bytes[1] = tmp[1];
			hdr->l2_hdr.d_addr_bytes[2] = tmp[2];
			hdr->l2_hdr.d_addr_bytes[3] = tmp[3];
			hdr->l2_hdr.d_addr_bytes[4] = tmp[4];
			hdr->l2_hdr.d_addr_bytes[5] = tmp[5];
			
			if (workload == 1) {
				uint32_t port = lookup_route((uint32_t) hdr->l3_hdr.dst_addr, _ip_list_gpu, _ip_list_len);

				// Result has to be written somewhere, otherwise the lookup_route call is removed by the compiler
				hdr->l3_hdr.time_to_live = port;	// TODO change this
			} else if (workload == 2) {
				// DOCA_GPUNETIO_DEVICE_GET_TIME(crc_start);
				// do {
				// 	DOCA_GPUNETIO_DEVICE_GET_TIME(crc_end);
				// } while ((crc_end - crc_start) < 320000);
				// // uint16_t tmp_crc = hdr->l3_hdr.hdr_checksum;
				uint32_t crc_accum = 0xffffffff;
				size = nbytes + sizeof(struct ether_hdr);

				char *data = (char *) buf_addr;
				for (j = 0, i = 0;  j < size;  j++ ) {
					i = ( (uint32_t) ( crc_accum >> 24) ^ *data++ ) & 0xff;
					crc_accum = ( crc_accum << 8 ) ^ crc_table_sh[i];
				}

				hdr->l3_hdr.hdr_checksum = (uint16_t)(crc_accum);
				// DOCA_GPUNETIO_DEVICE_GET_TIME(crc_end);
				// printf("CRC time %llu ns crc_accum %x\n", crc_end-crc_start, crc_accum);
			}

			doca_gpu_dev_eth_txq_send_enqueue_weak(txq, buf_ptr, nbytes + sizeof(struct ether_hdr), (curr_position + buf_idx) & mask_max_position, DOCA_GPU_SEND_FLAG_NONE); //DOCA_GPU_SEND_FLAG_NOTIFY
			// doca_gpu_dev_eth_txq_send_enqueue_strong(txq, buf_ptr, nbytes + sizeof(struct ether_hdr), DOCA_GPU_SEND_FLAG_NONE);

			buf_idx += blockDim.x;
		}
		__syncthreads();

		if (threadIdx.x == 0) {
			// printf("sending %d pkts in block %d curr_position %d\n", rx_pkt_num, blockIdx.x, curr_position);
			doca_gpu_dev_eth_txq_commit_weak(txq, rx_pkt_num);
			// doca_gpu_dev_eth_txq_commit_strong(txq);
			doca_gpu_dev_eth_txq_push(txq);
			//__threadfence_system();
		}
		__syncthreads();

		doca_gpu_dev_eth_txq_get_info(txq, &curr_position, &mask_max_position);
	}
}

extern "C" {

doca_error_t kernel_receive_packets(cudaStream_t stream, struct rxq_queue *rxq, uint32_t *gpu_exit_condition, uint8_t workload, ip_vector_t* _ip_list_gpu, uint32_t _ip_list_len, uint32_t *crc_table, uint32_t max_rx_num)
{
	cudaError_t result = cudaSuccess;

	if (rxq == NULL || rxq->numq == 0 || rxq->numq > MAX_QUEUES || gpu_exit_condition == NULL) {
		DOCA_LOG_ERR("kernel_receive_packets invalid input values");
		return DOCA_ERROR_INVALID_VALUE;
	}

	/* Check no previous CUDA errors */
	result = cudaGetLastError();
	if (cudaSuccess != result) {
		DOCA_LOG_ERR("[%s:%d] cuda failed with %s \n", __FILE__, __LINE__, cudaGetErrorString(result));
		return DOCA_ERROR_BAD_STATE;
	}

	receive_packets<<<rxq->numq, CUDA_THREADS, 0, stream>>>(rxq->eth_rxq_gpu[0], rxq->eth_rxq_gpu[1], rxq->eth_rxq_gpu[2], rxq->eth_rxq_gpu[3], \
													rxq->eth_rxq_gpu[4], rxq->eth_rxq_gpu[5], rxq->eth_rxq_gpu[6], rxq->eth_rxq_gpu[7], \
													rxq->eth_rxq_gpu[8], rxq->eth_rxq_gpu[9], rxq->eth_rxq_gpu[10], rxq->eth_rxq_gpu[11], \
													rxq->eth_rxq_gpu[12], rxq->eth_rxq_gpu[13], rxq->eth_rxq_gpu[14], rxq->eth_rxq_gpu[15], \
													gpu_exit_condition, \
													rxq->eth_txq_gpu[0], rxq->eth_txq_gpu[1], rxq->eth_txq_gpu[2], rxq->eth_txq_gpu[3], \
													rxq->eth_txq_gpu[4], rxq->eth_txq_gpu[5], rxq->eth_txq_gpu[6], rxq->eth_txq_gpu[7], \
													rxq->eth_txq_gpu[8], rxq->eth_txq_gpu[9], rxq->eth_txq_gpu[10], rxq->eth_txq_gpu[11], \
													rxq->eth_txq_gpu[12], rxq->eth_txq_gpu[13], rxq->eth_txq_gpu[14], rxq->eth_txq_gpu[15], \
													rxq->numq,
													workload,
													_ip_list_gpu, _ip_list_len,
													crc_table,
													max_rx_num);
	result = cudaGetLastError();
	if (cudaSuccess != result) {
		DOCA_LOG_ERR("[%s:%d] cuda failed with %s \n", __FILE__, __LINE__, cudaGetErrorString(result));
		return DOCA_ERROR_BAD_STATE;
	}

	return DOCA_SUCCESS;
}

} /* extern C */
