/*
 * driver/i2c.h — Host stub for ESP-IDF I2C master.
 *
 * The MCP4725 DAC uses I2C.  On the host writes are captured
 * for inspection; reads return configurable test values.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── types ─────────────────────────────────────────────────────── */
typedef int i2c_port_t;
#define I2C_NUM_0 0
#define I2C_NUM_1 1
#define I2C_MODE_MASTER 1

struct i2c_config_t {
    int mode;
    int sda_io_num, scl_io_num;
    int sda_pullup_en, scl_pullup_en;
    uint32_t clk_speed;
};

#define I2C_MASTER_FREQ_HZ               100000
#define I2C_MASTER_TX_BUF_DISABLE        0
#define I2C_MASTER_RX_BUF_DISABLE        0
#define I2C_MASTER_NACK                  0
#define I2C_MASTER_ACK                   1
#define ACK_CHECK_EN                     0x1
#define ACK_CHECK_DIS                    0x0

/* ── API stubs (C linkage — callable from firmware) ──────────── */
int i2c_param_config(i2c_port_t, const i2c_config_t*);
int i2c_driver_install(i2c_port_t, int, int, int, int, int);
int i2c_master_write_to_device(i2c_port_t, uint8_t addr,
                               const uint8_t* data, size_t len,
                               int timeout_ms);
int i2c_master_read_from_device(i2c_port_t, uint8_t addr,
                                uint8_t* data, size_t len,
                                int timeout_ms);
int i2c_master_write_read_device(i2c_port_t, uint8_t addr,
                                 const uint8_t* wdata, size_t wlen,
                                 uint8_t* rdata, size_t rlen,
                                 int timeout_ms);

#ifdef __cplusplus
}  // extern "C"
#endif

/*
 * Test harness API (C++ only — uses std::vector).
 *   i2c_test_get_last_write(port, addr) — returns bytes last written.
 *   i2c_test_set_read_response(port, addr, data, len) — sets response for next read.
 *   i2c_test_reset() — clears all virtual state.
 */
#ifdef __cplusplus
#include <vector>
const std::vector<uint8_t>& i2c_test_get_last_write(i2c_port_t port, uint8_t addr);
void i2c_test_set_read_response(i2c_port_t port, uint8_t addr,
                                const uint8_t* data, size_t len);
void i2c_test_reset(void);
#endif
