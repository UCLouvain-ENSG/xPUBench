#pragma once

#include <stdint.h>
#include "common_cross.h"

#define LOG_SQ_RING_DEPTH 7 /* 2^7 entries, max is 2^15 */
#define LOG_RQ_RING_DEPTH 7 /* 2^7 entries, max is 2^15 */
#define LOG_CQ_RING_DEPTH 7 /* 2^7 entries, max is 2^15 */
#define CRC_TABLE_SIZE 256  /* CRC table size */
#define MAX_LINE_LENGTH 1000000
#define LOG_WQ_DATA_ENTRY_BSIZE 11 /* WQ buffer logarithmic size */

#define SIZE_ETHER_HDR 14
#define DST_ADDR_OFFSET 16

typedef struct {
    uint32_t addr;
    uint32_t mask;
    uint32_t gw;
    uint32_t port;
    uint32_t extra;
} ip_vector_t;
