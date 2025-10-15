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

#include <rte_ethdev.h>
#include <rte_pmd_mlx5.h>
#include <rte_mbuf_dyn.h>

#include <doca_dpdk.h>
#include <doca_rdma_bridge.h>
#include <doca_flow.h>
#include <doca_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "gpunetio_common.h"

#include "common.h"

#define FLOW_NB_COUNTERS 524228 /* 1024 x 512 */
#define MBUF_NUM 8192
#define MBUF_SIZE 2048

#define MAX_LINE_LENGTH 1000000

#define POLYNOMIAL 0x04c11db7L

struct doca_flow_port *df_port;
bool force_quit;

DOCA_LOG_REGISTER(GPU_DMABUF:SAMPLE);

/*
 * DOCA PE callback to be invoked if any Eth Txq get an error
 * sending packets.
 *
 * @event_error [in]: DOCA PE event error handler
 * @event_user_data [in]: custom user data set at registration time
 */
void error_send_packet_cb(struct doca_eth_txq_gpu_event_error_send_packet *event_error, union doca_data event_user_data)
{
	uint16_t packet_index;

	doca_eth_txq_gpu_event_error_send_packet_get_position(event_error, &packet_index);
	DOCA_LOG_INFO("Error in send queue %ld, packet %d. Gracefully killing the app",
		      event_user_data.u64,
		      packet_index);
	DOCA_GPUNETIO_VOLATILE(force_quit) = true;
}

/*
 * DOCA PE callback to be invoked on ICMP Eth Txq to get the debug info
 * when sending packets
 *
 * @event_notify [in]: DOCA PE event debug handler
 * @event_user_data [in]: custom user data set at registration time
 */
void correct_send_packet_cb(struct doca_eth_txq_gpu_event_notify_send_packet *event_notify,
			       union doca_data event_user_data)
{
	uint16_t packet_index;
	uint64_t packet_timestamp;

	doca_eth_txq_gpu_event_notify_send_packet_get_position(event_notify, &packet_index);
	doca_eth_txq_gpu_event_notify_send_packet_get_timestamp(event_notify, &packet_timestamp);

	DOCA_LOG_INFO("Send correct event: Queue %ld packet %d sent at %ld time",
		      event_user_data.u64,
		      packet_index,
		      packet_timestamp);
}

/* generate the table of CRC remainders for all possible bytes */
void gen_crc_table(uint32_t *crc_table) {
	register int i, j;
	register uint32_t crc_accum;
	for ( i = 0;  i < 256;  i++ ) {
		crc_accum = ( (uint32_t) i << 24 );
        for ( j = 0;  j < 8;  j++ ) {
			if ( crc_accum & 0x80000000L ) crc_accum = ( crc_accum << 1 ) ^ POLYNOMIAL;
            else crc_accum = ( crc_accum << 1 ); }
        crc_table[i] = crc_accum; }
   return;
}

uint32_t read_from_file(ip_vector_t** input_vector, char *file_name) {

    FILE *fin = fopen(file_name, "rb");
    if (!fin) {
        perror("Failed to open file");
        return -1;
    }

    char line[MAX_LINE_LENGTH];
    if (!fgets(line, sizeof(line), fin)) {
        perror("Failed to read line");
        fclose(fin);
        return -1;
    }

    uint32_t size = strtoul(line, NULL, 10);
    ip_vector_t* _ip_vector_cpu = (ip_vector_t*)malloc(size * sizeof(ip_vector_t));
    if (!_ip_vector_cpu) {
        perror("Failed to allocate memory");
        fclose(fin);
        return -1;
    }

    for (uint32_t i = 0; i < size; i++) {
        if (!fgets(line, sizeof(line), fin)) {
            perror("Failed to read line");
            free(_ip_vector_cpu);
            fclose(fin);
            return -1;
        }
        _ip_vector_cpu[i].addr = strtoul(line, NULL, 10);
		// printf("addr[%d]: %d\n", i, _ip_vector_cpu[i].addr);

        if (!fgets(line, sizeof(line), fin)) {
            perror("Failed to read line");
            free(_ip_vector_cpu);
            fclose(fin);
            return -1;
        }
        _ip_vector_cpu[i].mask = strtoul(line, NULL, 10);
		// printf("mask[%d]: %d\n", i, _ip_vector_cpu[i].mask);

        if (!fgets(line, sizeof(line), fin)) {
            perror("Failed to read line");
            free(_ip_vector_cpu);
            fclose(fin);
            return -1;
        }
        _ip_vector_cpu[i].gw = strtoul(line, NULL, 10);
		// printf("gw[%d]: %d\n", i, _ip_vector_cpu[i].gw);

        if (!fgets(line, sizeof(line), fin)) {
            perror("Failed to read line");
            free(_ip_vector_cpu);
            fclose(fin);
            return -1;
        }
        _ip_vector_cpu[i].port = strtoul(line, NULL, 10);
		// printf("port[%d]: %d\n", i, _ip_vector_cpu[i].port);

        if (!fgets(line, sizeof(line), fin)) {
            perror("Failed to read line");
            free(_ip_vector_cpu);
            fclose(fin);
            return -1;
        }
        _ip_vector_cpu[i].extra = strtoul(line, NULL, 10);
		// printf("extra[%d]: %d\n", i, _ip_vector_cpu[i].extra);
    }

    fclose(fin);

	// printf("test: %d\n", _ip_vector_cpu[1].addr);
	// printf("test: %d\n", _ip_vector_cpu[1].mask);
	// printf("test: %d\n", _ip_vector_cpu[1].gw);
	// printf("test: %d\n", _ip_vector_cpu[1].port);
	// printf("test: %d\n", _ip_vector_cpu[1].extra);

	*input_vector = _ip_vector_cpu;
    return size;
}



/*
 * Signal handler to quit application gracefully
 *
 * @signum [in]: signal received
 */
static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		DOCA_LOG_INFO("Signal %d received, preparing to exit!", signum);
		DOCA_GPUNETIO_VOLATILE(force_quit) = true;
	}
}

/*
 * Initialize a DOCA network device.
 *
 * @nic_pcie_addr [in]: Network card PCIe address
 * @ddev [out]: DOCA device
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t init_doca_device(char *nic_pcie_addr, struct doca_dev **ddev)
{
	doca_error_t result;

	if (nic_pcie_addr == NULL || ddev == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	if (strnlen(nic_pcie_addr, DOCA_DEVINFO_PCI_ADDR_SIZE) >= DOCA_DEVINFO_PCI_ADDR_SIZE)
		return DOCA_ERROR_INVALID_VALUE;

	result = open_doca_device_with_pci(nic_pcie_addr, NULL, ddev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open NIC device based on PCI address");
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Init doca flow.
 *
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t init_doca_flow(int queue_num)
{
	struct doca_flow_cfg *queue_flow_cfg;
	doca_error_t result;

	/* Initialize doca flow framework */
	result = doca_flow_cfg_create(&queue_flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_cfg: %s", doca_error_get_descr(result));
		return result;
	}
	result = doca_flow_cfg_set_pipe_queues(queue_flow_cfg, queue_num);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg pipe_queues: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(queue_flow_cfg);
		return result;
	}
	result = doca_flow_cfg_set_mode_args(queue_flow_cfg, "vnf,hws,isolated,use_doca_eth");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg mode_args: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(queue_flow_cfg);
		return result;
	}
	result = doca_flow_cfg_set_nr_counters(queue_flow_cfg, FLOW_NB_COUNTERS);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_counters: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(queue_flow_cfg);
		return result;
	}
	result = doca_flow_init(queue_flow_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init doca flow with: %s", doca_error_get_descr(result));
		doca_flow_cfg_destroy(queue_flow_cfg);
		return result;
	}
	doca_flow_cfg_destroy(queue_flow_cfg);
	return DOCA_SUCCESS;
}

/*
 * Start doca flow.
 *
 * @dev [in]: DOCA device
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t start_doca_flow(struct doca_dev *dev)
{
	struct doca_flow_port_cfg *port_cfg;
	doca_error_t result;

	/* Start doca flow port */
	result = doca_flow_port_cfg_create(&port_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_port_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_port_cfg_set_dev(port_cfg, dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_port_cfg dev: %s", doca_error_get_descr(result));
		doca_flow_port_cfg_destroy(port_cfg);
		return result;
	}

	result = doca_flow_port_start(port_cfg, &df_port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start doca flow port with: %s", doca_error_get_descr(result));
		doca_flow_port_cfg_destroy(port_cfg);
		return result;
	}

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Flow UDP pipeline
 *
 * @rxq [in]: Receive queue handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_udp_pipe(struct rxq_queue *rxq)
{
	doca_error_t result;
	struct doca_flow_match match = {0};
	struct doca_flow_match match_mask = {0};
	struct doca_flow_fwd fwd = {0};
	struct doca_flow_fwd miss_fwd = {0};
	struct doca_flow_pipe_cfg *pipe_cfg;
	const char *pipe_name = "GPU_RXQ_UDP_PIPE";
	uint16_t flow_queue_id;
	uint16_t rss_queues[MAX_QUEUES];
	struct doca_flow_monitor monitor = {
		.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED,
	};


	if (rxq == NULL || df_port == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP;

	for (int idx = 0; idx < rxq->numq; idx++) {
		doca_eth_rxq_get_flow_queue_id(rxq->eth_rxq_cpu[idx], &flow_queue_id);
		rss_queues[idx] = flow_queue_id;
	}

	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_queues = rss_queues;
	fwd.rss_outer_flags = DOCA_FLOW_RSS_IPV4 | DOCA_FLOW_RSS_UDP;
	fwd.num_of_queues = rxq->numq;

	miss_fwd.type = DOCA_FLOW_FWD_DROP;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, df_port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_pipe_cfg_set_name(pipe_cfg, pipe_name);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg name: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg type: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_is_root(pipe_cfg, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg is_root: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_enable_strict_matching(pipe_cfg, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg enable_strict_matching: %s",
			     doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, &match_mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg monitor: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, rxq->numq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg_set_nr_entries monitor: %s", doca_error_get_descr(result));
		doca_flow_pipe_cfg_destroy(pipe_cfg);
		return result;
	}

	result = doca_flow_pipe_cfg_set_miss_counter(pipe_cfg, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg_set_miss_counter monitor: %s", doca_error_get_descr(result));
		doca_flow_pipe_cfg_destroy(pipe_cfg);
		return result;
	}

	result = doca_flow_pipe_create(pipe_cfg, &fwd, &miss_fwd, &(rxq->rxq_pipe));
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RxQ pipe creation failed with: %s", doca_error_get_descr(result));
		return result;
	}
	doca_flow_pipe_cfg_destroy(pipe_cfg);

	/* Add HW offload */
	result = doca_flow_pipe_add_entry(0, rxq->rxq_pipe, &match, NULL, NULL, NULL, DOCA_FLOW_NO_WAIT, NULL, &rxq->rxq_pipe_entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RxQ pipe entry creation failed with: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(df_port, 0, 0, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("RxQ pipe entry process failed with: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_DBG("Created Pipe");

	return DOCA_SUCCESS;

destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Create DOCA Flow root pipeline
 *
 * @rxq [in]: Receive queue handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t create_root_pipe(struct rxq_queue *rxq)
{
	doca_error_t result;
	struct doca_flow_match match_mask = {0};
	struct doca_flow_monitor monitor = {
		.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED,
	};
	struct doca_flow_match udp_match = {
		.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4,
		.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_UDP,
	};

	struct doca_flow_fwd udp_fwd = {
		.type = DOCA_FLOW_FWD_PIPE,
	};
	struct doca_flow_pipe_cfg *pipe_cfg;
	const char *pipe_name = "ROOT_PIPE";

	if (rxq == NULL)
		return DOCA_ERROR_INVALID_VALUE;

	udp_fwd.next_pipe = rxq->rxq_pipe;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, df_port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_pipe_cfg_set_name(pipe_cfg, pipe_name);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg name: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_CONTROL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg type: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_is_root(pipe_cfg, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg is_root: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_enable_strict_matching(pipe_cfg, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg enable_strict_matching: %s",
			     doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, NULL, &match_mask);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg monitor: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_create(pipe_cfg, NULL, NULL, &rxq->root_pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Root pipe creation failed with: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	doca_flow_pipe_cfg_destroy(pipe_cfg);


	result = doca_flow_pipe_control_add_entry(0,
						  0,
						  rxq->root_pipe,
						  &udp_match,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  &udp_fwd,
						  NULL,
						  &rxq->root_udp_entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Root pipe UDP entry creation failed with: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(df_port, 0, 0, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Root pipe entry process failed with: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_DBG("Created Root pipe");

	return DOCA_SUCCESS;

destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Destroy DOCA Ethernet Tx queue for GPU
 *
 * @rxq [in]: DOCA Eth Rx queue handler
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t destroy_rxq(struct rxq_queue *rxq)
{
	doca_error_t result;

	if (rxq == NULL) {
		DOCA_LOG_ERR("Can't destroy UDP queues, invalid input");
		return DOCA_ERROR_INVALID_VALUE;
	}

	DOCA_LOG_INFO("Destroying Rxq");

	if (rxq->root_pipe != NULL) {
		doca_flow_pipe_destroy(rxq->root_pipe);
	}
	if (rxq->rxq_pipe != NULL) {
		doca_flow_pipe_destroy(rxq->rxq_pipe);
	}

	for (int idx = 0; idx < rxq->numq; idx++) {
		result = doca_ctx_stop(rxq->eth_rxq_ctx[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_ctx_stop: %s", doca_error_get_descr(result));
			return DOCA_ERROR_BAD_STATE;
		}
	}

	for (int idx = 0; idx < rxq->numq; idx++) {
		result = doca_gpu_mem_free(rxq->gpu_dev, rxq->gpu_pkt_addr[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to free gpu memory: %s", doca_error_get_descr(result));
			return DOCA_ERROR_BAD_STATE;
		}
	}


	for (int idx = 0; idx < rxq->numq; idx++) {
		result = doca_eth_rxq_destroy(rxq->eth_rxq_cpu[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_eth_rxq_destroy: %s", doca_error_get_descr(result));
			return DOCA_ERROR_BAD_STATE;
		}
	}

	if (df_port != NULL) {
		result = doca_flow_port_stop(df_port);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to stop DOCA flow port, err: %s", doca_error_get_name(result));
			return DOCA_ERROR_BAD_STATE;
		}

		doca_flow_destroy();
	}


	for (int idx = 0; idx < rxq->numq; idx++) {
		result = doca_mmap_destroy(rxq->pkt_buff_mmap[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to destroy mmap: %s", doca_error_get_descr(result));
			return DOCA_ERROR_BAD_STATE;
		}
	}

	// result = doca_dev_close(rxq->ddev);
	// if (result != DOCA_SUCCESS) {
	// 	DOCA_LOG_ERR("Failed to destroy Eth dev: %s", doca_error_get_descr(result));
	// 	return DOCA_ERROR_BAD_STATE;
	// }

	return DOCA_SUCCESS;
}

/*
 * Create DOCA Ethernet Tx queue for GPU
 *
 * @rxq [in]: DOCA Eth Tx queue handler
 * @gpu_dev [in]: DOCA GPUNetIO device
 * @ddev [in]: DOCA device
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_rxq(struct rxq_queue *rxq, struct doca_gpu *gpu_dev, struct doca_dev *ddev, uint32_t queue_num, uint32_t max_pkt_num)
{
	doca_error_t result;
	uint32_t cyclic_buffer_size = 0;
	union doca_data event_user_data[MAX_QUEUES] = {0};

	if (rxq == NULL || gpu_dev == NULL || ddev == NULL || queue_num == 0) {
		DOCA_LOG_ERR("Can't create UDP queues, invalid input");
		return DOCA_ERROR_INVALID_VALUE;
	}

	rxq->gpu_dev = gpu_dev;
	rxq->ddev = ddev;
	rxq->port = df_port;
	rxq->numq = queue_num;

	for (uint32_t idx = 0; idx < queue_num; idx++) {
		DOCA_LOG_INFO("Creating Sample Eth Rxq and Txq %d\n", idx);
	
		result = doca_eth_rxq_create(rxq->ddev, max_pkt_num, MAX_PKT_SIZE, &(rxq->eth_rxq_cpu[idx]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_eth_rxq_create: %s", doca_error_get_descr(result));
			return DOCA_ERROR_BAD_STATE;
		}

		result = doca_eth_rxq_set_type(rxq->eth_rxq_cpu[idx], DOCA_ETH_RXQ_TYPE_CYCLIC);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_eth_rxq_set_type: %s", doca_error_get_descr(result));
			return DOCA_ERROR_BAD_STATE;
		}

		result = doca_eth_rxq_estimate_packet_buf_size(DOCA_ETH_RXQ_TYPE_CYCLIC,
						       0,
						       0,
						       MAX_PKT_SIZE,
						       max_pkt_num,
						       0,
						       &cyclic_buffer_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to get eth_rxq cyclic buffer size: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_mmap_create(&rxq->pkt_buff_mmap[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create mmap: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_mmap_add_dev(rxq->pkt_buff_mmap[idx], rxq->ddev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to add dev to mmap: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_gpu_mem_alloc(rxq->gpu_dev, cyclic_buffer_size, GPU_PAGE_SIZE, DOCA_GPU_MEM_TYPE_GPU, &rxq->gpu_pkt_addr[idx], NULL);
		if (result != DOCA_SUCCESS || rxq->gpu_pkt_addr[idx] == NULL) {
			DOCA_LOG_ERR("Failed to allocate gpu memory %s", doca_error_get_descr(result));
			goto exit_error;
		}

		/* Map GPU memory buffer used to receive packets with DMABuf */
		DOCA_LOG_INFO("Mapping receive queue buffer (0x%p size %dB) with nvidia-peermem mode",
			rxq->gpu_pkt_addr[idx], cyclic_buffer_size);

		/* If failed, use nvidia-peermem legacy method */
		result = doca_mmap_set_memrange(rxq->pkt_buff_mmap[idx], rxq->gpu_pkt_addr[idx], cyclic_buffer_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set memrange for mmap %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_mmap_set_permissions(rxq->pkt_buff_mmap[idx], DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set permissions for mmap %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_mmap_start(rxq->pkt_buff_mmap[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to start mmap %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_eth_rxq_set_pkt_buf(rxq->eth_rxq_cpu[idx], rxq->pkt_buff_mmap[idx], 0, cyclic_buffer_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set cyclic buffer  %s", doca_error_get_descr(result));
			goto exit_error;
		}

		rxq->eth_rxq_ctx[idx] = doca_eth_rxq_as_doca_ctx(rxq->eth_rxq_cpu[idx]);
		if (rxq->eth_rxq_ctx[idx] == NULL) {
			DOCA_LOG_ERR("Failed doca_eth_rxq_as_doca_ctx: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_ctx_set_datapath_on_gpu(rxq->eth_rxq_ctx[idx], rxq->gpu_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_ctx_set_datapath_on_gpu: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_ctx_start(rxq->eth_rxq_ctx[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_ctx_start: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_eth_rxq_get_gpu_handle(rxq->eth_rxq_cpu[idx], &(rxq->eth_rxq_gpu[idx]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_eth_rxq_get_gpu_handle: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		////// setting txq
		result = doca_eth_txq_create(rxq->ddev, MAX_SQ_DESCR_NUM, &(rxq->eth_txq_cpu[idx]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_eth_txq_create: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_eth_txq_set_l3_chksum_offload(rxq->eth_txq_cpu[idx], 1);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to set eth_txq l3 offloads: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		rxq->eth_txq_ctx[idx] = doca_eth_txq_as_doca_ctx(rxq->eth_txq_cpu[idx]);
		if (rxq->eth_txq_ctx[idx] == NULL) {
			DOCA_LOG_ERR("Failed doca_eth_txq_as_doca_ctx: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_ctx_set_datapath_on_gpu(rxq->eth_txq_ctx[idx], rxq->gpu_dev);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_ctx_set_datapath_on_gpu: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_pe_create(&rxq->txq_pe[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to create pe queue: %s", doca_error_get_descr(result));
			return EXIT_FAILURE;
		}

		event_user_data[idx].u64 = idx;
		result =
			doca_eth_txq_gpu_event_error_send_packet_register(rxq->eth_txq_cpu[idx],
										error_send_packet_cb,
										event_user_data[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to set DOCA progress engine callback: %s",
						doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_eth_txq_gpu_event_notify_send_packet_register(rxq->eth_txq_cpu[idx],
										correct_send_packet_cb,
										event_user_data[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to set DOCA progress engine callback: %s",
						doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_pe_connect_ctx(rxq->txq_pe[idx], rxq->eth_txq_ctx[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to set DOCA progress engine to DOCA Eth Txq: %s",
						doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_ctx_start(rxq->eth_txq_ctx[idx]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_ctx_start: %s", doca_error_get_descr(result));
			goto exit_error;
		}

		result = doca_eth_txq_get_gpu_handle(rxq->eth_txq_cpu[idx], &(rxq->eth_txq_gpu[idx]));
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed doca_eth_txq_get_gpu_handle: %s", doca_error_get_descr(result));
			goto exit_error;
		}
	}

	/////// done

	/* Create UDP based flow pipe */
	result = create_udp_pipe(rxq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_udp_pipe returned %s", doca_error_get_descr(result));
		goto exit_error;
	}

	/* Create root pipe with UDP pipe as unique entry */
	result = create_root_pipe(rxq);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_root_pipe returned %s", doca_error_get_descr(result));
		goto exit_error;
	}

	return DOCA_SUCCESS;

exit_error:
	destroy_rxq(rxq);
	return DOCA_ERROR_BAD_STATE;
}

/*
 * Launch GPUNetIO simple receive sample
 *
 * @sample_cfg [in]: Sample config parameters
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
doca_error_t gpunetio_simple_receive(struct sample_send_wait_cfg *sample_cfg)
{
	doca_error_t result;
	struct doca_gpu *gpu_dev = NULL;
	struct doca_dev *ddev = NULL;
	struct rxq_queue rxq = {0};
	cudaStream_t stream;
	cudaError_t res_rt = cudaSuccess;
	uint32_t *cpu_exit_condition;
	uint32_t *gpu_exit_condition;
	struct doca_flow_resource_query query_stats;

	result = init_doca_device(sample_cfg->nic_pcie_addr, &ddev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function init_doca_device returned %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	result = init_doca_flow(sample_cfg->queue_num);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function init_doca_flow returned %d:%s", result, doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	result = start_doca_flow(ddev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function start_doca_flow returned %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	/* Gracefully terminate sample if ctrlc */
	DOCA_GPUNETIO_VOLATILE(force_quit) = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	result = doca_gpu_create(sample_cfg->gpu_pcie_addr, &gpu_dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_gpu_create returned %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	result = create_rxq(&rxq, gpu_dev, ddev, sample_cfg->queue_num, sample_cfg->max_pkt_num);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function create_rxq returned %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	res_rt = cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
	if (res_rt != cudaSuccess) {
		DOCA_LOG_ERR("Function cudaStreamCreateWithFlags error %d", res_rt);
		return DOCA_ERROR_DRIVER;
	}

	result = doca_gpu_mem_alloc(gpu_dev,
				    sizeof(uint32_t),
				    GPU_PAGE_SIZE,
				    DOCA_GPU_MEM_TYPE_GPU_CPU,
				    (void **)&gpu_exit_condition,
				    (void **)&cpu_exit_condition);
	if (result != DOCA_SUCCESS || gpu_exit_condition == NULL || cpu_exit_condition == NULL) {
		DOCA_LOG_ERR("Function doca_gpu_mem_alloc returned %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	DOCA_LOG_INFO("Warmup CUDA kernel");

	DOCA_GPUNETIO_VOLATILE(cpu_exit_condition[0]) = 0;
	kernel_receive_packets(stream, &rxq, gpu_exit_condition, 0xFF, NULL, 0, NULL, sample_cfg->batch);
	DOCA_GPUNETIO_VOLATILE(cpu_exit_condition[0]) = 1;
	cudaStreamSynchronize(stream);
	DOCA_GPUNETIO_VOLATILE(cpu_exit_condition[0]) = 0;

	ip_vector_t *_ip_list_gpu;
	ip_vector_t* _ip_vector_cpu;
	uint32_t _ip_list_len = 0;
	uint32_t _crc_table[256];
	uint32_t *_gpu_table = NULL;
	cudaError_t code;

	if (sample_cfg->workload == 1) {
		_ip_list_len = read_from_file(&_ip_vector_cpu, sample_cfg->iplookup_cache_file);
        DOCA_LOG_INFO("Cache file read. Table size: %d",_ip_list_len);

		code = cudaMalloc((void**)&_ip_list_gpu, _ip_list_len*sizeof(ip_vector_t));
		if (code != cudaSuccess) 
		{
			fprintf(stderr,"GPUassert: %s\n", cudaGetErrorString(code));
		}

		code = cudaMemcpy(_ip_list_gpu, _ip_vector_cpu, _ip_list_len*sizeof(ip_vector_t), cudaMemcpyHostToDevice);

		if (code != cudaSuccess) 
		{
			fprintf(stderr,"GPUassert: %s\n", cudaGetErrorString(code));
		}
	} else if (sample_cfg->workload == 2) {
		gen_crc_table(_crc_table);
		cudaMalloc((void**)&_gpu_table, 256 * sizeof(uint32_t));
		cudaMemcpy(_gpu_table, _crc_table, 256 * sizeof(uint32_t), cudaMemcpyHostToDevice);
	}

	DOCA_LOG_INFO("Launching CUDA kernel to receive packets");

	printf("EVENT APP_READY\n");
	kernel_receive_packets(stream, &rxq, gpu_exit_condition, sample_cfg->workload, _ip_list_gpu, _ip_list_len, _gpu_table, sample_cfg->batch);

	DOCA_LOG_INFO("Waiting for termination");
	/* This loop keeps busy main thread until force_quit is set to 1 (e.g. typing ctrl+c) */
	while (DOCA_GPUNETIO_VOLATILE(force_quit) == false) {
		for (int idx = 0; idx < rxq.numq; idx++) {
			doca_pe_progress(rxq.txq_pe[idx]);
		}
		usleep(100000);
	}

	DOCA_GPUNETIO_VOLATILE(cpu_exit_condition[0]) = 1;

	DOCA_LOG_INFO("Exiting from sample");

	cudaStreamSynchronize(stream);

	result = doca_flow_resource_query_pipe_miss(rxq.rxq_pipe, &query_stats);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function doca_flow_resource_query_pipe_miss returned %s", doca_error_get_descr(result));
		return EXIT_FAILURE;
	}

	DOCA_LOG_INFO("UDP miss packets %ld", query_stats.counter.total_pkts);

	struct doca_flow_resource_query query_stats_entry;

	result = doca_flow_resource_query_entry(rxq.rxq_pipe_entry, &query_stats_entry);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query root entry: %s", doca_error_get_descr(result));
		return result;
	}

	DOCA_LOG_INFO("UDP rx packets %ld", query_stats_entry.counter.total_pkts);

	result = destroy_rxq(&rxq);

	if (sample_cfg->workload == 1) {
		free(_ip_vector_cpu);
		cudaFree(_ip_list_gpu);
	}
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Function destroy_rxq returned %s", doca_error_get_descr(result));
		return DOCA_ERROR_BAD_STATE;
	}

	DOCA_LOG_INFO("Sample finished successfully");

	return DOCA_SUCCESS;
}
