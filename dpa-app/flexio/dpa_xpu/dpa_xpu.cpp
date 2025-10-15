#include "dpa_xpu.h"
#include <gflags/gflags.h>

DEFINE_string(device_name, "", "device name for select ib device");

DEFINE_uint64(begin_thread, 0,
              "begin thread number, which means use DPA core [begin_thread, "
              "begin_thread + g_thread_num]");

DEFINE_uint32(g_thread_num, 1, "thread count for test");

DEFINE_bool(compute_crc, false, "Compute CRC for packets");

DEFINE_string(
    ip_lookup, "",
    "Path to IP lookup table");

DEFINE_bool(buffer_on_host, true, "Use host buffer for RQ");

DEFINE_bool(shared_rx_buffer, true, "Use shared RX buffer for RQ");
