#include "common/dpa_xpu_common.h"
#include "wrapper_flexio.hpp"
#include <gflags/gflags.h>

extern "C" {
flexio_func_t dpa_xpu_device_event_handler;
flexio_func_t dpa_xpu_device_init;
extern struct flexio_app *dpa_xpu_device;
}
#define TARGET_MAC 0x040000000000

DECLARE_string(device_name);

DECLARE_uint64(begin_thread);

DECLARE_uint32(g_thread_num);

DECLARE_bool(compute_crc);

DECLARE_string(ip_lookup);

DECLARE_bool(buffer_on_host);

DECLARE_bool(shared_rx_buffer);


class dpa_xpu_config {
  public:
    FLEX::CQ *rq_cq;
    FLEX::CQ *sq_cq;
    FLEX::SQ *sq;
    FLEX::RQ *rq;

    FLEX::dr_flow_rule *rx_flow_rule;

    FLEX::dr_flow_rule *tx_flow_root_rule;
    FLEX::dr_flow_rule *tx_flow_rule;
};
