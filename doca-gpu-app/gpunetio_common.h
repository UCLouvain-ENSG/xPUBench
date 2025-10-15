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

#ifndef GPUNETIO_SEND_WAIT_TIME_COMMON_H_
#define GPUNETIO_SEND_WAIT_TIME_COMMON_H_

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <signal.h>

#include <cuda.h>
#include <cuda_runtime.h>

#include <doca_error.h>
#include <doca_dev.h>
#include <doca_mmap.h>
#include <doca_gpunetio.h>
#include <doca_eth_rxq.h>
#include <doca_eth_txq.h>
#include <doca_buf_array.h>
#include <doca_pe.h>
#include <doca_eth_txq_gpu_data_path.h>

#include "common.h"

#define GPU_PAGE_SIZE (1UL << 16)
#define MAX_PCI_ADDRESS_LEN 32U
//#define CUDA_BLOCK_THREADS 32 //unused
#define MAX_CACHE_FILE_LEN 1024
#define CUDA_THREADS 512
//#define PACKET_SIZE 1024 //unused
#define ETHER_ADDR_LEN 6
#define MAX_PKT_NUM 65536 //Queue size in RX
#define MAX_PKT_SIZE 2048
#define MAX_RX_TIMEOUT_NS 5000000 // 500us
#define MAX_RX_NUM_PKTS 8192
#define MAX_SQ_DESCR_NUM 32768
#define MAX_QUEUES 16
#define MAX_WORKLOAD 3

#define BE_TO_CPU_16(v) \
	(uint16_t)((v>>8)|(v<<8))

struct ether_hdr {
	uint8_t d_addr_bytes[ETHER_ADDR_LEN]; /* Destination addr bytes in tx order */
	uint8_t s_addr_bytes[ETHER_ADDR_LEN]; /* Source addr bytes in tx order */
	uint16_t ether_type;		      /* Frame type */
} __attribute__((__packed__));

struct ipv4_hdr {
	uint8_t version_ihl;	  /* version and header length */
	uint8_t type_of_service;  /* type of service */
	uint16_t total_length;	  /* length of packet */
	uint16_t packet_id;	  /* packet ID */
	uint16_t fragment_offset; /* fragmentation offset */
	uint8_t time_to_live;	  /* time to live */
	uint8_t next_proto_id;	  /* protocol ID */
	uint16_t hdr_checksum;	  /* header checksum */
	uint32_t src_addr;	  /* source address */
	uint32_t dst_addr;	  /* destination address */
} __attribute__((__packed__));

struct udp_hdr {
	uint16_t src_port;    /* UDP source port */
	uint16_t dst_port;    /* UDP destination port */
	uint16_t dgram_len;   /* UDP datagram length */
	uint16_t dgram_cksum; /* UDP datagram checksum */
} __attribute__((__packed__));

struct eth_ip_udp_hdr {
	struct ether_hdr l2_hdr; /* Ethernet header */
	struct ipv4_hdr l3_hdr;	 /* IP header */
	struct udp_hdr l4_hdr;	 /* UDP header */
} __attribute__((__packed__));

typedef struct {
    uint32_t addr;
    uint32_t mask;
    uint32_t gw;
    uint32_t port;
    uint32_t extra;
} ip_vector_t;

/* Application configuration structure */
struct sample_send_wait_cfg {
	char gpu_pcie_addr[MAX_PCI_ADDRESS_LEN];	/* GPU PCIe address */
	char nic_pcie_addr[MAX_PCI_ADDRESS_LEN];	/* Network card PCIe address */
	uint8_t queue_num;				/* Number of GPU receive quMaxeues */
	uint8_t workload;				/* Number of GPU receive queues */
    char *iplookup_cache_file;		/* Path to the IPLookup cache file */
	uint32_t batch;					/* Size of batches*/
	uint32_t max_pkt_num;				/* Queue size in RX (default: MAX_PKT_NUM) */
	ip_vector_t* _ip_vector_cpu;	/* IP List for IP Lookup*/
};

/* Send queues objects */
struct rxq_queue {
	struct doca_gpu *gpu_dev;		/* GPUNetio handler associated to queues*/
	struct doca_dev *ddev;			/* DOCA device handler associated to queues */

	uint16_t numq;						/* Number of queues */
	struct doca_ctx *eth_rxq_ctx[MAX_QUEUES];		/* DOCA Ethernet send queue context */
	struct doca_eth_rxq *eth_rxq_cpu[MAX_QUEUES];	/* DOCA Ethernet send queue CPU handler */
	struct doca_gpu_eth_rxq *eth_rxq_gpu[MAX_QUEUES];	/* DOCA Ethernet send queue GPU handler */
	struct doca_mmap *pkt_buff_mmap[MAX_QUEUES];	/* DOCA mmap to receive packet with DOCA Ethernet queue */
	void *gpu_pkt_addr[MAX_QUEUES];			/* DOCA mmap GPU memory address */
	int dmabuf_fd[MAX_QUEUES];				/* GPU memory dmabuf descriptor */

	struct doca_flow_port *port;				/* DOCA Flow port */
	struct doca_flow_pipe *rxq_pipe;			/* DOCA Flow receive pipe */
	struct doca_flow_pipe_entry *rxq_pipe_entry;			/* DOCA Flow receive pipe */
	struct doca_flow_pipe *root_pipe;			/* DOCA Flow root pipe */
	struct doca_flow_pipe_entry *root_udp_entry;		/* DOCA Flow root entry */

	struct doca_ctx *eth_txq_ctx[MAX_QUEUES];		/* DOCA Ethernet send queue context */
	struct doca_eth_txq *eth_txq_cpu[MAX_QUEUES];		/* DOCA Ethernet send queue CPU handler */
	struct doca_gpu_eth_txq *eth_txq_gpu[MAX_QUEUES];	/* DOCA Ethernet send queue GPU handler */

	struct doca_pe *txq_pe[MAX_QUEUES];
};

/*
 * Launch GPUNetIO simple receive sample
 *
 * @sample_cfg [in]: Sample config parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t gpunetio_simple_receive(struct sample_send_wait_cfg *sample_cfg);

#if __cplusplus
extern "C" {
#endif

/*
 * Launch a CUDA kernel to send packets with wait on time feature.
 *
 * @stream [in]: CUDA stream to launch the kernel
 * @rxq [in]: DOCA Eth Tx queue to use to send packets
 * @gpu_exit_condition [in]: exit from CUDA kernel
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t kernel_receive_packets(cudaStream_t stream, struct rxq_queue *rxq, uint32_t *gpu_exit_condition, uint8_t workload, ip_vector_t* _ip_list_gpu, uint32_t _ip_list_len, uint32_t *crc_table, uint32_t max_rx_num);

#if __cplusplus
}
#endif
#endif
