/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <libflexio-dev/flexio_dev.h>
#include <stddef.h>
#include <stdbool.h>
#include "wrapper_flexio_device.h"
#include "../common/dpa_xpu_common.h"

flexio_dev_rpc_handler_t
    dpa_xpu_device_init; /* Device initialization function */
flexio_dev_event_handler_t
    dpa_xpu_device_event_handler; /* Event handler function */

#define MAX_THREADS 256

// ONLY_RECV = 1 means only receive packet, never refactor packet
// ONLY_RECV = 0 means receive and refactor any packet
#define ONLY_RECV 0

// DISPLAY_COUNT = 1 means print the counter for refactor packet
// DISPLAY_COUNT = 0 means doesn't print the counter for refactor packet
#define DISPLAY_COUNT 1
/* Device context */
static struct device_context {
    uint32_t lkey;            /* Local memory key */
    uint32_t is_initalized;   /* Initialization flag */
    struct cq_ctx_t rqcq_ctx; /* RQ CQ context */
    struct cq_ctx_t sqcq_ctx; /* SQ CQ context */
    struct rq_ctx_t rq_ctx;   /* RQ context */
    struct sq_ctx_t sq_ctx;   /* SQ context */
    struct dt_ctx_t dt_ctx;   /* DT context */
    struct host_rq_ctx_t host_rq_ctx;
    uint32_t packets_count; /* Number of processed packets */
    uint8_t rq_on_host;
    uint8_t sq_on_host;
    ip_vector_t *ip_table;
    uint32_t ip_table_len; /* Length of the IP table */
    uint32_t port_counter;
    uint32_t *crc_table;
    uint32_t crc_counter;
} __attribute__((__aligned__(64))) dev_ctxs[MAX_THREADS];

/*
 * Initialize the CQ context
 *
 * @app_cq [in]: CQ HW context
 * @ctx [out]: CQ context
 */
static void init_cq(const struct app_transfer_cq app_cq, struct cq_ctx_t *ctx) {
    ctx->cq_number = app_cq.cq_num;
    ctx->cq_ring = (struct flexio_dev_cqe64 *)app_cq.cq_ring_daddr;
    ctx->cq_dbr = (uint32_t *)app_cq.cq_dbr_daddr;

    ctx->cqe = ctx->cq_ring; /* Points to the first CQE */
    ctx->cq_idx = 0;
    ctx->cq_hw_owner_bit = 0x1;
    ctx->cq_idx_mask = ((1 << LOG_CQ_RING_DEPTH) - 1);
}

/*
 * Initialize the RQ context
 *
 * @app_rq [in]: RQ HW context
 * @ctx [out]: RQ context
 */
static void init_rq(const struct app_transfer_wq app_rq, struct rq_ctx_t *ctx) {
    ctx->rq_number = app_rq.wq_num;
    ctx->rq_ring = (struct flexio_dev_wqe_rcv_data_seg *)app_rq.wq_ring_daddr;
    ctx->rq_dbr = (uint32_t *)app_rq.wq_dbr_daddr;
    ctx->rq_idx_mask = ((1 << LOG_RQ_RING_DEPTH) - 1);
}

/*
 * Initialize the SQ context
 *
 * @app_sq [in]: SQ HW context
 * @ctx [out]: SQ context
 */
static void init_sq(const struct app_transfer_wq app_sq, struct sq_ctx_t *ctx) {
    ctx->sq_number = app_sq.wq_num;
    ctx->sq_ring = (union flexio_dev_sqe_seg *)app_sq.wq_ring_daddr;
    ctx->sq_dbr = (uint32_t *)app_sq.wq_dbr_daddr;

    ctx->sq_wqe_seg_idx = 0;
    ctx->sq_dbr++;
    ctx->sq_idx_mask = ((1 << (LOG_SQ_RING_DEPTH + LOG_SQE_NUM_SEGS)) - 1);
}

/*
 * This is the main function of the dpa net device, called on each packet from
 * dpa_refactor_device_event_handler() Packet are received from the RQ,
 * processed by changing MAC addresses and transmitted to the SQ.
 *
 * @dtctx [in]: This thread context
 */

static void __unused receive_packet_on_dpa(struct flexio_dev_thread_ctx *dtctx,
                                           struct device_context *dev_ctx) {
    uint32_t data_sz;
    char *rq_data;

    /* Extract relevant data from CQE */
    rq_data = receive_packet(&dev_ctx->rqcq_ctx, &dev_ctx->rq_ctx, &data_sz);
    // arp, just drop
    // if (__builtin_expect((data_sz != LOG2VALUE(LOG_WQ_DATA_ENTRY_BSIZE)), 0))
    // {
    //     flexio_dev_print("unexcpet size %u %p\n", data_sz, (void *)rq_data);
    //     return;
    // }
    (void)dtctx;
    (void)rq_data;
    return;
}

bool matches_prefix(uint32_t addr1, uint32_t addr2, uint32_t mask) {
    return ((addr1 ^ addr2) & mask) == 0;
}

bool mask_as_specific(uint32_t mask, uint32_t addr) {
    return (addr & mask) == mask;
}

int lookup_entry(uint32_t a, ip_vector_t *ip_list, uint32_t len) {
    int found = -1;
    for (uint32_t i = 0; i < len - 1; i++) {
        ip_vector_t r = ip_list[i];
        bool b = matches_prefix(a, r.addr, r.mask);
        if (b) {
            found = i;
            for (uint32_t j = r.extra; j < len - 1; j++) {
                ip_vector_t s = ip_list[j];
                bool c = (matches_prefix(a, s.addr, s.mask) &&
                          mask_as_specific(r.mask, s.mask));
                if (c)
                    found = j;
            }
        }
    }
    return found;
}

uint32_t lookup_route(uint32_t a, ip_vector_t *ip_list, uint32_t len) {
    int ei = lookup_entry(a, ip_list, len);

    if (ei >= 0) {
        return ip_list[ei].port;
    } else
        return 0;
}
static uint32_t compute_crc(char *packet, uint32_t packet_size,
                            uint32_t *crc_table) {
    // Compute the crc for the packet
    // This is a placeholder function, actual implementation may vary
    // depending on the crc algorithm used.
    int i;
    uint32_t j;
    uint32_t crc_accum = 0xffffffff;
    for (j = 0; j < packet_size; j++) {
        i = ((uint32_t)(crc_accum >> 24) ^ *packet++) & 0xff;
        crc_accum = (crc_accum << 8) ^ crc_table[i];
    }
    return crc_accum;
}

static void process_packet(struct flexio_dev_thread_ctx *dtctx,
                           struct device_context *dev_ctx) {
    uint32_t data_sz;
    char *rq_data;

    /* Extract relevant data from CQE */
    rq_data = receive_packet(&dev_ctx->rqcq_ctx, &dev_ctx->rq_ctx, &data_sz);
    // arp, just drop
    // if (__builtin_expect((data_sz != LOG2VALUE(LOG_WQ_DATA_ENTRY_BSIZE)),
    // 0))
    // {
    //     flexio_dev_print("unexcpet size %u %p\n", data_sz, (void
    //     *)rq_data); return;
    // }

    char *sq_data;
    sq_data = rq_data;
    if (dev_ctx->crc_table) {
        dev_ctx->crc_counter +=
            compute_crc(rq_data, data_sz, dev_ctx->crc_table);
    }
    if (dev_ctx->ip_table) {
        dev_ctx->port_counter += lookup_route(
            *(uint32_t *)(rq_data + SIZE_ETHER_HDR + DST_ADDR_OFFSET),
            dev_ctx->ip_table, dev_ctx->ip_table_len);
    }

    size_t src_mac = *(size_t *)sq_data;
    size_t dst_mac = *(size_t *)(sq_data + 6);
    *(size_t *)(sq_data) = dst_mac;
    *(size_t *)(sq_data + 6) =
        (src_mac & 0x0000FFFFFFFFFFFF) | (0x0008ll << 48);
    // source and dst mac byte by byte
    // flexio_dev_print("src_mac %02x:%02x:%02x:%02x:%02x:%02x\n",
    //        (unsigned char)(src_mac >> 40), (unsigned char)(src_mac >> 32),
    //        (unsigned char)(src_mac >> 24), (unsigned char)(src_mac >> 16),
    //        (unsigned char)(src_mac >> 8), (unsigned char)src_mac);
    // flexio_dev_print("dst_mac %02x:%02x:%02x:%02x:%02x:%02x\n",
    //           (unsigned char)(dst_mac >> 40), (unsigned char)(dst_mac >> 32),
    //           (unsigned char)(dst_mac >> 24), (unsigned char)(dst_mac >> 16),
    //           (unsigned char)(dst_mac >> 8), (unsigned char)dst_mac);

    prepare_send_packet(&dev_ctx->sq_ctx, sq_data, data_sz);

    finish_send_packet(dtctx, &dev_ctx->sq_ctx);
}

static void __unused receive_packet_on_host(struct flexio_dev_thread_ctx *dtctx,
                                            struct device_context *dev_ctx) {
    uint32_t data_sz;
    char *rq_data;

    /* Extract relevant data from CQE */
    rq_data = receive_packet(&dev_ctx->rqcq_ctx, &dev_ctx->rq_ctx, &data_sz);
    // arp or LLDP, just drop
    // if (__builtin_expect((data_sz != LOG2VALUE(LOG_WQ_DATA_ENTRY_BSIZE)),
    // 0))
    // {
    //     flexio_dev_print("unexcpet size %u %p\n", data_sz, (void
    //     *)rq_data); return;
    // }
    (void)dtctx;
    (void)rq_data;
    return;
}
static void process_packet_host(struct flexio_dev_thread_ctx *dtctx,
                                struct device_context *dev_ctx) {
    uint32_t data_sz;
    char *rq_data;

    /* Extract relevant data from CQE */
    rq_data = receive_packet(&dev_ctx->rqcq_ctx, &dev_ctx->rq_ctx, &data_sz);
    // arp or LLDP, just drop
    // if (__builtin_expect((data_sz != LOG2VALUE(LOG_WQ_DATA_ENTRY_BSIZE)),
    // 0))
    // {
    //     flexio_dev_print("unexcpet size %u %p\n", data_sz, (void
    //     *)rq_data); return;
    // }

    char *rq_data_dpa;
    char *sq_data;
    char *sq_data_dpa;

    rq_data_dpa = host_rq_addr_to_dpa_addr(rq_data, &dev_ctx->host_rq_ctx);
    if (dev_ctx->crc_table) {
        dev_ctx->crc_counter +=
            compute_crc(rq_data_dpa, data_sz, dev_ctx->crc_table);
    }
    if (dev_ctx->ip_table) {
        dev_ctx->port_counter += lookup_route(
            *(uint32_t *)(rq_data_dpa + SIZE_ETHER_HDR + DST_ADDR_OFFSET),
            dev_ctx->ip_table, dev_ctx->ip_table_len);
    }

    sq_data = rq_data;
    sq_data_dpa = rq_data_dpa;
    // print mac here
    flexio_dev_print("dst_mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                     (unsigned char)(*(size_t *)sq_data_dpa >> 40),
                     (unsigned char)(*(size_t *)sq_data_dpa >> 32),
                     (unsigned char)(*(size_t *)sq_data_dpa >> 24),
                     (unsigned char)(*(size_t *)sq_data_dpa >> 16),
                     (unsigned char)(*(size_t *)sq_data_dpa >> 8),
                     (unsigned char)*(size_t *)sq_data_dpa);
    flexio_dev_print("src_mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                     (unsigned char)(*(size_t *)(sq_data_dpa + 6) >> 40),
                     (unsigned char)(*(size_t *)(sq_data_dpa + 6) >> 32),
                     (unsigned char)(*(size_t *)(sq_data_dpa + 6) >> 24),
                     (unsigned char)(*(size_t *)(sq_data_dpa + 6) >> 16),
                     (unsigned char)(*(size_t *)(sq_data_dpa + 6) >> 8),
                     (unsigned char)*(size_t *)(sq_data_dpa + 6));

    size_t src_mac = *(size_t *)sq_data_dpa;

    size_t dst_mac = *(size_t *)(sq_data_dpa + 6);

    *(size_t *)(sq_data_dpa) = dst_mac;
    *(size_t *)(sq_data_dpa + 6) =
        (src_mac & 0x0000FFFFFFFFFFFF) | (0x0008ll << 48);

    prepare_send_packet(&dev_ctx->sq_ctx, sq_data, data_sz);

    finish_send_packet_host(dtctx, &dev_ctx->sq_ctx);
}

/*
 * Called by host to initialize the device context
 *
 * @data [in]: pointer to the device context from the host
 * @return: This function always returns 0
 */
__dpa_rpc__ uint64_t dpa_xpu_device_init(uint64_t data) {
    struct queue_config_data *shared_data = (struct queue_config_data *)data;
    Assert(shared_data->thread_index < MAX_THREADS);

    struct device_context *dev_ctx = &dev_ctxs[shared_data->thread_index];
    dev_ctx->lkey = shared_data->sq_data.wqd_mkey_id;
    dev_ctx->host_rq_ctx.rkey = shared_data->rq_data.wqd_mkey_id;
    // this is trick used field
    dev_ctx->host_rq_ctx.rq_window_id = shared_data->new_buffer_mkey_id;
    dev_ctx->host_rq_ctx.host_rx_buff = (void *)shared_data->rq_data.wqd_daddr;

    init_cq(shared_data->rq_cq_data, &dev_ctx->rqcq_ctx);
    init_rq(shared_data->rq_data, &dev_ctx->rq_ctx);
    init_cq(shared_data->sq_cq_data, &dev_ctx->sqcq_ctx);
    init_sq(shared_data->sq_data, &dev_ctx->sq_ctx);

    init_send_sq(&dev_ctx->sq_ctx, LOG2VALUE(LOG_SQ_RING_DEPTH), dev_ctx->lkey);
    dev_ctx->dt_ctx.sq_tx_buff = (void *)shared_data->sq_data.wqd_daddr;
    dev_ctx->dt_ctx.tx_buff_idx = 0;
    dev_ctx->dt_ctx.data_idx_mask = ((1 << (LOG_SQ_RING_DEPTH)) - 1);

    dev_ctx->rq_on_host = shared_data->rq_data.daddr_on_host;
    dev_ctx->sq_on_host = shared_data->sq_data.daddr_on_host;

    dev_ctx->is_initalized = 1;
    dev_ctx->crc_table = (uint32_t *)shared_data->crc_table;
    // for (size_t i = 0; i < CRC_TABLE_SIZE; i++) {
    //     flexio_dev_print("crc_table[%zu] = %u\n", i,
    //     dev_ctx->crc_table[i]);
    // }
    dev_ctx->ip_table = (ip_vector_t *)shared_data->ip_table;
    dev_ctx->ip_table_len = shared_data->ip_table_len;
    // flexio_dev_print("ip_table_len %u\n", dev_ctx->ip_table_len);
    // for (size_t i = 0; i < dev_ctx->ip_table_len; i++) {
    //     flexio_dev_print("addr[%zu] = %u\n", i, dev_ctx->ip_table[i].addr);
    //     flexio_dev_print("mask[%zu] = %u\n", i, dev_ctx->ip_table[i].mask);
    //     flexio_dev_print("gw[%zu] = %u\n", i, dev_ctx->ip_table[i].gw);
    //     flexio_dev_print("port[%zu] = %u\n", i, dev_ctx->ip_table[i].port);
    //     flexio_dev_print("extra[%zu] = %u\n", i, dev_ctx->ip_table[i].extra);
    // }
    LOG_I("Thread %u init success\n", shared_data->thread_index);
    return 0;
}

/*
 * This function is called when a new packet is received to RQ's CQ.
 * Upon receiving a packet, the function will iterate over all received
 * packets and process them. Once all packets in the CQ are processed, the
 * CQ will be rearmed to receive new packets events.
 */
void __dpa_global__ dpa_xpu_device_event_handler(uint64_t index) {
    struct flexio_dev_thread_ctx *dtctx;
    struct device_context *dev_ctx = &dev_ctxs[index];
    flexio_dev_get_thread_ctx(&dtctx);

    if (dev_ctx->is_initalized == 0) {
        flexio_dev_thread_reschedule();
    }

    // flexio_dev_print("thread %ld begin\n", index);

    uint8_t rq_on_host = dev_ctx->rq_on_host;
    if (rq_on_host) {
        dev_ctx->host_rq_ctx.dpa_rx_buff = get_host_buffer_with_dtctx(
            dtctx, dev_ctx->host_rq_ctx.rq_window_id, dev_ctx->host_rq_ctx.rkey,
            dev_ctx->host_rq_ctx.host_rx_buff);
    } else {
        // in this situation, we set dpa_buff to host_buff, to prevent
        // misuse of receive_packet_host
        dev_ctx->host_rq_ctx.dpa_rx_buff =
            (flexio_uintptr_t)dev_ctx->host_rq_ctx.host_rx_buff;
    }

#if DISPLAY_COUNT
    register size_t pkt_count = 0;
#endif
#if ONLY_RECV
    uint8_t send_initial = 0;
#endif
    while (dtctx != NULL) {
        while (flexio_dev_cqe_get_owner(dev_ctx->rqcq_ctx.cqe) !=
               dev_ctx->rqcq_ctx.cq_hw_owner_bit) {
            if (rq_on_host) {
#if ONLY_RECV
                if (!send_initial) {
                    process_packet_host(dtctx, dev_ctx);
                    send_initial = 1;
                } else {
                    receive_packet_on_host(dtctx, dev_ctx);
                }
#else
                process_packet_host(dtctx, dev_ctx);
#endif
            } else {
#if ONLY_RECV
                if (!send_initial) {
                    process_packet(dtctx, dev_ctx);
                    send_initial = 1;
                } else {
                    receive_packet_on_dpa(dtctx, dev_ctx);
                }
#else
                process_packet(dtctx, dev_ctx);
#endif
            }
            step_rq(&dev_ctx->rq_ctx);
            step_cq(&dev_ctx->rqcq_ctx);
#if DISPLAY_COUNT
            pkt_count++;
            flexio_dev_print("thread %ld pkt_count %zu\n", index, pkt_count);

            if (__builtin_expect((pkt_count == 1000000), 0)) {
                dev_ctx->packets_count += 1000000;

                pkt_count = 0;
                // __dpa_thread_memory_writeback();

                if (index == 0) {
                    size_t sum = dev_ctxs[index].packets_count;
                    // size_t sum = 0;
                    // for (size_t i = 0; i < MAX_THREADS - 1; i += 16) {
                    //     flexio_dev_print("%3ld - %3ld:", i, i + 15);

                    //     for (size_t j = i; j <= i + 15 && j < MAX_THREADS
                    //     - 1; j++) {
                    //         sum += dev_ctxs[j].packets_count;
                    //         flexio_dev_print(" %u",
                    //         dev_ctxs[j].packets_count);
                    //     }
                    //     flexio_dev_print("\n");
                    // }
                    flexio_dev_print("sum : %ld\n", sum);
                }
            }
#endif
        }
    }
    flexio_dev_print("thread %ld end\n", index);
    flexio_dev_print("port_counter %d end\n", dev_ctx->port_counter);
    flexio_dev_print("crc_counter %d end\n", dev_ctx->crc_counter);

    __dpa_thread_fence(__DPA_MEMORY, __DPA_W, __DPA_W);
    flexio_dev_cq_arm(dtctx, dev_ctx->rqcq_ctx.cq_idx,
                      dev_ctx->rqcq_ctx.cq_number);
    flexio_dev_thread_reschedule();
}
