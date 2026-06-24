/*
 * driver/twai.h — Host stub for ESP-IDF TWAI (Two-Wire Automotive Interface).
 *
 * On real hardware:    TWAI peripheral manages CAN frames, bit timing,
 *                       error counters, bus-off recovery.
 * On host (HOST_BUILD): Routes all CAN traffic through VirtualCanBus.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── types matching ESP-IDF twai.h ─────────────────────────────── */

typedef int twai_mode_t;
#define TWAI_MODE_NORMAL  0
#define TWAI_MODE_NO_ACK  1
#define TWAI_MODE_LISTEN_ONLY 2

typedef int gpio_num_t;

struct twai_general_config_t {
    int          controller_id;
    twai_mode_t  mode;
    gpio_num_t   tx_io;
    gpio_num_t   rx_io;
    gpio_num_t   clkout_io;
    gpio_num_t   bus_off_io;
    uint32_t     tx_queue_len;
    uint32_t     rx_queue_len;
    uint32_t     alerts_enabled;
    uint32_t     clkout_divider;
    int          intr_flags;
};

struct twai_timing_config_t {
    uint32_t quanta_resolution_hz;
    int      tseg_1;
    int      tseg_2;
    int      sjw;
};

struct twai_filter_config_t {
    int  acceptance_code;
    int  acceptance_mask;
    bool single_filter;
};

struct twai_message_t {
    uint32_t identifier;
    uint8_t  data_length_code;
    uint8_t  data[8];
    union {
        uint32_t flags;
        struct {
            unsigned extd : 1;
            unsigned rtr  : 1;
            unsigned ss   : 1;
            unsigned self : 1;
        };
    };
    // Convenience
    bool extd;
    bool self;
    bool ss;
};

struct twai_status_info_t {
    uint32_t tx_error_counter;
    uint32_t rx_error_counter;
    uint32_t msgs_to_tx;
    uint32_t msgs_to_rx;
    uint32_t tx_failed_count;
    uint32_t rx_missed_count;
    uint32_t rx_overrun_count;
    uint32_t arb_lost_count;
    uint32_t bus_error_count;
    int      state;
};

#define TWAI_STATE_STOPPED 0
#define TWAI_STATE_RUNNING 1
#define TWAI_STATE_BUS_OFF 2

/* ── ESP-IDF error codes ──────────────────────────────────────── */

#define ESP_OK                   0
#define ESP_FAIL                -1
#define ESP_ERR_INVALID_ARG     -2
#define ESP_ERR_INVALID_STATE   -3
#define ESP_ERR_TIMEOUT         -4
#define ESP_ERR_NO_MEM          -5
#define ESP_ERR_NOT_SUPPORTED   -6

/* ── convenience macros ────────────────────────────────────────── */

#define TWAI_GENERAL_CONFIG_DEFAULT_V2(ctrl, tx, rx, mode)      \
    twai_general_config_t{ ctrl, mode, tx, rx,                  \
        gpio_num_t(-1), gpio_num_t(-1), 32, 32, 0, 0, 0 }

#define TWAI_TIMING_CONFIG_500KBITS()                           \
    twai_timing_config_t{ 8000000, 11, 4, 2 }

#define TWAI_TIMING_CONFIG_250KBITS()                           \
    twai_timing_config_t{ 8000000, 14, 7, 3 }

#define TWAI_FILTER_CONFIG_ACCEPT_ALL()                         \
    twai_filter_config_t{ 0, 0, true }

/* ── API stubs — routed through VirtualCanBus at link time ────── */

int  twai_driver_install(const twai_general_config_t*  g,
                         const twai_timing_config_t*   t,
                         const twai_filter_config_t*   f);
int  twai_driver_uninstall(void);
int  twai_start(void);
int  twai_stop(void);
int  twai_transmit(const twai_message_t* msg, int timeout_ms);
int  twai_receive(twai_message_t* msg, int timeout_ms);
int  twai_get_status_info(twai_status_info_t* info);

#ifdef __cplusplus
}
#endif
