#include "common/dpa_xpu_common.h"
#include "dpa_xpu.h"

DOCA_LOG_REGISTER(DPA_XPU);

#define POLYNOMIAL 0x04c11db7L
#define PKTGEN_MAC 0x000000000000

static bool force_quit;

static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        /* Add additional new lines for output readability */
        DOCA_LOG_INFO(" ");
        DOCA_LOG_INFO("Signal %d received, preparing to exit", signum);
        DOCA_LOG_INFO(" ");
        force_quit = true;
    }
}

// 是否使用nic mode，使用nic
// mode时候config.tx_flow_root_rule->create_dr_rule这行代码会segment
// fault，原因未知，暂时注释掉 不影响测试
bool nic_mode = false;

uint32_t read_from_file(ip_vector_t **input_vector, const char *file_name) {

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
    ip_vector_t *_ip_vector_cpu =
        static_cast<ip_vector_t *>(malloc(size * sizeof(ip_vector_t)));
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

int main(int argc, char **argv) {

    gflags::ParseCommandLineFlags(&argc, &argv, true);
    std::string device_name = FLAGS_device_name;
    auto *global_ctx = new FLEX::Context(device_name);
    Assert(global_ctx);
    // useless at DOCA2.2.1
    // global_ctx->alloc_devx_uar(0x1);
    global_ctx->alloc_pd();
    global_ctx->create_process(dpa_xpu_device);
    global_ctx->create_window();
    global_ctx->generate_flexio_uar();
    global_ctx->print_init("dpa_xpu", 0);

    bool rq_buffer_on_host = FLAGS_buffer_on_host;
    bool tx_shared_rx_buffer = FLAGS_shared_rx_buffer;

    auto *global_rx_dr = new FLEX::DR(global_ctx, MLX5DV_DR_DOMAIN_TYPE_NIC_RX);
    auto *global_tx_dr = new FLEX::DR(global_ctx, MLX5DV_DR_DOMAIN_TYPE_FDB);

    FLEX::flow_matcher matcher{};

    matcher.set_dst_mac_mask();
    FLEX::dr_flow_table *global_rx_flow_table =
        global_rx_dr->create_flow_table(0, 0, &matcher);
    matcher.clear();
    matcher.set_src_mac_mask();
    FLEX::dr_flow_table *global_tx_flow_root_table =
        global_tx_dr->create_flow_table(0, 0, &matcher);
    FLEX::dr_flow_table *global_tx_flow_table =
        global_tx_dr->create_flow_table(1, 0, &matcher);
    matcher.clear();

    std::vector<dpa_xpu_config> configs;

    ip_vector_t *ip_vector_cpu = nullptr;
    uint32_t ip_table_len = 0;
    flexio_uintptr_t ip_table_dev = 0;
    flexio_uintptr_t crc_table_dev = 0;
    if (FLAGS_ip_lookup.length() > 0) {
        printf("File path: %s\n", FLAGS_ip_lookup.c_str());
        ip_table_len = read_from_file(&ip_vector_cpu, FLAGS_ip_lookup.c_str());
        Assert(ip_table_len > 0);
        // for(uint32_t i = 0; i < ip_table_len; i++) {
        //     printf("addr[%d]: %u\n", i, ip_vector_cpu[i].addr);
        //     printf("mask[%d]: %u\n", i, ip_vector_cpu[i].mask);
        //     printf("gw[%d]: %u\n", i, ip_vector_cpu[i].gw);
        //     printf("port[%d]: %u\n", i, ip_vector_cpu[i].port);
        //     printf("extra[%d]: %u\n", i, ip_vector_cpu[i].extra);
        // }
        printf("ip_table_len: %u\n", ip_table_len);
        Assert(flexio_buf_dev_alloc(global_ctx->get_process(),
                                    ip_table_len * sizeof(ip_vector_t),
                                    &ip_table_dev) == FLEXIO_STATUS_SUCCESS);

        Assert(flexio_host2dev_memcpy(global_ctx->get_process(), ip_vector_cpu,
                                      ip_table_len * sizeof(ip_vector_t),
                                      ip_table_dev) == FLEXIO_STATUS_SUCCESS);
    }
    if (FLAGS_compute_crc) {
        uint32_t crc_table[CRC_TABLE_SIZE];
        int i, j;
        uint32_t crc_accum;
        Assert(flexio_buf_dev_alloc(global_ctx->get_process(),
                                    sizeof(crc_table),
                                    &crc_table_dev) == FLEXIO_STATUS_SUCCESS);
        for (i = 0; i < CRC_TABLE_SIZE; i++) {
            crc_accum = (static_cast<uint32_t>(i)) << 24;
            for (j = 0; j < 8; j++) {
                if (crc_accum & 0x80000000L)
                    crc_accum = (crc_accum << 1) ^ POLYNOMIAL;
                else
                    crc_accum = (crc_accum << 1);
            }
            crc_table[i] = crc_accum;
            // printf("crc_table[%d] = %u\n", i, crc_table[i]);
        }
        Assert(flexio_host2dev_memcpy(global_ctx->get_process(), crc_table,
                                      sizeof(crc_table),
                                      crc_table_dev) == FLEXIO_STATUS_SUCCESS);
        DOCA_LOG_INFO("CRC table loaded, size");
    }
    for (size_t i = 0; i < FLAGS_g_thread_num; i++) {
        dpa_xpu_config config{};
        global_ctx->create_event_handler(dpa_xpu_device_event_handler);
        config.rq_cq = new FLEX::CQ(true, LOG_CQ_RING_DEPTH, global_ctx, i);
        config.sq_cq = new FLEX::CQ(false, LOG_CQ_RING_DEPTH, global_ctx, i);

        config.rq = new FLEX::RQ(LOG_RQ_RING_DEPTH, LOG_WQ_DATA_ENTRY_BSIZE,
                                 config.rq_cq->get_cq_num(), global_ctx,
                                 rq_buffer_on_host);

        if (tx_shared_rx_buffer) {
            config.sq =
                new FLEX::SQ(LOG_SQ_RING_DEPTH, LOG_WQ_DATA_ENTRY_BSIZE,
                             config.sq_cq->get_cq_num(), global_ctx, config.rq);
        } else {
            config.sq = new FLEX::SQ(LOG_SQ_RING_DEPTH, LOG_WQ_DATA_ENTRY_BSIZE,
                                     config.sq_cq->get_cq_num(), global_ctx);
        }

        queue_config_data dev_data{
            config.rq_cq->get_cq_transf(),
            config.rq->get_rq_transf(),
            config.sq_cq->get_cq_transf(),
            config.sq->get_sq_transf(),
            static_cast<uint32_t>(i + FLAGS_begin_thread),
            0,
            global_ctx->get_window_id(),
            ip_table_dev,
            ip_table_len,
            crc_table_dev};
        flexio_uintptr_t dev_config_data = global_ctx->move_to_dev(dev_data);
        uint64_t rpc_ret_val;

        Assert(flexio_process_call(global_ctx->get_process(),
                                   &dpa_xpu_device_init, &rpc_ret_val,
                                   dev_config_data) == FLEXIO_STATUS_SUCCESS);
        config.rx_flow_rule = new FLEX::dr_flow_rule();
        config.rx_flow_rule->add_dest_devx_tir(config.rq->get_inner_ptr());
        matcher.set_dst_mac(TARGET_MAC + i + FLAGS_begin_thread);
        config.rx_flow_rule->create_dr_rule(global_rx_flow_table, &matcher);
        matcher.clear();

        config.tx_flow_rule = new FLEX::dr_flow_rule();
        config.tx_flow_root_rule = new FLEX::dr_flow_rule();
        config.tx_flow_root_rule->add_dest_table(
            global_tx_flow_table->dr_table);
        config.tx_flow_rule->add_dest_vport(global_tx_dr->get_inner_ptr(),
                                            0xFFFF);
        matcher.set_src_mac(TARGET_MAC + i + FLAGS_begin_thread);
        if (!nic_mode) {
            config.tx_flow_root_rule->create_dr_rule(global_tx_flow_root_table,
                                                     &matcher);
        } else {
            (void)global_tx_flow_root_table;
        }
        config.tx_flow_rule->create_dr_rule(global_tx_flow_table, &matcher);
        matcher.clear();
        // thread 0 need to handle latency threads
        if (i == 0) {
            printf("Handling latency packets\n");
            config.rx_flow_rule = new FLEX::dr_flow_rule();
            config.rx_flow_rule->add_dest_devx_tir(config.rq->get_inner_ptr());
            matcher.set_dst_mac(PKTGEN_MAC);
            config.rx_flow_rule->create_dr_rule(global_rx_flow_table, &matcher);
            matcher.clear();

            config.tx_flow_rule = new FLEX::dr_flow_rule();
            config.tx_flow_root_rule = new FLEX::dr_flow_rule();
            config.tx_flow_root_rule->add_dest_table(
                global_tx_flow_table->dr_table);
            config.tx_flow_rule->add_dest_vport(global_tx_dr->get_inner_ptr(),
                                                0xFFFF);
            matcher.set_src_mac(PKTGEN_MAC);
            if (!nic_mode) {
                config.tx_flow_root_rule->create_dr_rule(
                    global_tx_flow_root_table, &matcher);
            } else {
                (void)global_tx_flow_root_table;
            }
            config.tx_flow_rule->create_dr_rule(global_tx_flow_table, &matcher);
            matcher.clear();
        }

        global_ctx->event_handler_run(i + FLAGS_begin_thread, i);
        configs.push_back(config);
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    DOCA_LOG_INFO("MT L2 reflector Started");
    /* Add an additional new line for output readability */
    DOCA_LOG_INFO("Press Ctrl+C to terminate");
    printf("EVENT dpa-ready\n");
    while (!force_quit)
        sleep(1);

    // don't free at now
    return EXIT_SUCCESS;
}
