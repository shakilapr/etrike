#pragma once
// Minimal TWAI (Two-Wire Automotive Interface) mock for host testing.
// Provides the types and macros used by shared/can/can_driver.h.

#include <cstdint>
#include <cstring>

// ── TWAI modes ─────────────────────────────────────────────────────
enum { TWAI_MODE_NORMAL = 0 };

// ── TWAI message ───────────────────────────────────────────────────
struct twai_message_t {
    uint32_t identifier = 0;
    uint32_t extd       = 0;
    uint8_t  data_length_code = 0;
    uint8_t  data[8]    = {};
    uint8_t  self       = 0;
    uint8_t  ss         = 1;
};

// ── GPIO type (ESP-IDF v5.x) ─────────────────────────────────────────
typedef int gpio_num_t;

// ── TWAI configuration types ───────────────────────────────────────
struct twai_general_config_t {
    int tx_io     = 5;
    int rx_io     = 4;
    int mode      = 0;
    int tx_queue_len = 5;
    int rx_queue_len = 5;
    uint32_t alerts_enabled = 0;
    uint32_t clkout_divider = 0;
    int intr_flags = 0;
};

struct twai_timing_config_t {
    uint32_t quanta_resolution_hz = 8'000'000;  // ESP-IDF v5.x
    uint32_t brp         = 0;
    uint8_t  tseg_1      = 0;
    uint8_t  tseg_2      = 0;
    uint8_t  sjw         = 0;
    uint32_t triple_sampling = 0;
};

struct twai_filter_config_t {
    uint32_t acceptance_code = 0;
    uint32_t acceptance_mask = 0xFFFFFFFF;
    bool     single_filter   = true;
};

struct twai_status_info_t {
    uint32_t state             = 0;
    uint32_t msgs_to_tx        = 0;
    uint32_t msgs_to_rx        = 0;
    uint32_t tx_error_counter  = 0;
    uint32_t rx_error_counter  = 0;
    uint32_t tx_failed_count   = 0;
    uint32_t rx_missed_count   = 0;
    uint32_t rx_overrun_count  = 0;
    uint32_t arb_lost_count    = 0;
    uint32_t bus_error_count   = 0;
};

// ── Configuration macros ───────────────────────────────────────────
#define TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, mode) \
    twai_general_config_t{ (tx), (rx), (mode), 5, 5, 0, 0, 0 }

#define TWAI_GENERAL_CONFIG_DEFAULT_V2(ctrl_id, tx, rx, mode) \
    twai_general_config_t{ (int)(tx), (int)(rx), (mode), 5, 5, 0, 0, 0 }

inline twai_timing_config_t TWAI_TIMING_CONFIG_500KBITS() {
    twai_timing_config_t t = {};
    t.brp    = 8;
    t.tseg_1 = 15;
    t.tseg_2 = 4;
    t.sjw    = 3;
    return t;
}

inline twai_timing_config_t TWAI_TIMING_CONFIG_250KBITS() {
    twai_timing_config_t t = {};
    t.brp    = 8;
    t.tseg_1 = 15;
    t.tseg_2 = 4;
    t.sjw    = 3;
    return t;
}

inline twai_filter_config_t TWAI_FILTER_CONFIG_ACCEPT_ALL() {
    twai_filter_config_t f = {};
    f.acceptance_code = 0;
    f.acceptance_mask = 0xFFFFFFFF;
    f.single_filter   = true;
    return f;
}

// ── API stubs ──────────────────────────────────────────────────────
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107

inline int twai_driver_install(const twai_general_config_t*, const twai_timing_config_t*,
                                const twai_filter_config_t*) { return 0; }
inline int twai_start() { return 0; }
inline int twai_stop() { return 0; }
inline int twai_driver_uninstall() { return 0; }
inline int twai_receive(twai_message_t*, uint32_t) { return 0; }
inline int twai_transmit(const twai_message_t*, uint32_t) { return 0; }
inline int twai_get_status_info(twai_status_info_t*) { return 0; }
