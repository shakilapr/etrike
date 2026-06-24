/*
 * i2c_stubs.cpp — Host implementation: virtual I2C bus.
 *
 * Captures writes for inspection; returns configurable read responses.
 */
#include "driver/i2c.h"
#include "esp_err.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <mutex>

struct I2cDevice {
    std::vector<uint8_t> last_write;
    std::vector<uint8_t> read_response;
};

static std::mutex g_i2c_mutex;
static std::map<int, std::map<uint8_t, I2cDevice>> g_devices;  // port -> addr -> device

extern "C" {

int i2c_param_config(i2c_port_t port, const i2c_config_t* cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    printf("[i2c] port %d configured (SDA=%d, SCL=%d, %d Hz)\n",
           port, cfg->sda_io_num, cfg->scl_io_num, cfg->clk_speed);
    return ESP_OK;
}

int i2c_driver_install(i2c_port_t port, int, int, int, int, int) {
    printf("[i2c] port %d driver installed\n", port);
    return ESP_OK;
}

int i2c_master_write_to_device(i2c_port_t port, uint8_t addr,
                               const uint8_t* data, size_t len,
                               int) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(g_i2c_mutex);
    auto& dev = g_devices[port][addr];
    dev.last_write.assign(data, data + len);
    return ESP_OK;
}

int i2c_master_read_from_device(i2c_port_t port, uint8_t addr,
                                uint8_t* data, size_t len,
                                int) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(g_i2c_mutex);
    auto& dev = g_devices[port][addr];
    if (dev.read_response.size() >= len) {
        std::memcpy(data, dev.read_response.data(), len);
    } else {
        std::memset(data, 0, len);
    }
    return ESP_OK;
}

int i2c_master_write_read_device(i2c_port_t port, uint8_t addr,
                                 const uint8_t* wdata, size_t wlen,
                                 uint8_t* rdata, size_t rlen,
                                 int) {
    i2c_master_write_to_device(port, addr, wdata, wlen, 0);
    return i2c_master_read_from_device(port, addr, rdata, rlen, 0);
}

} // extern "C"

/* ── test harness API ──────────────────────────────────────────── */

const std::vector<uint8_t>& i2c_test_get_last_write(i2c_port_t port, uint8_t addr) {
    static std::vector<uint8_t> empty;
    std::lock_guard<std::mutex> lock(g_i2c_mutex);
    auto it = g_devices.find(port);
    if (it == g_devices.end()) return empty;
    auto it2 = it->second.find(addr);
    if (it2 == it->second.end()) return empty;
    return it2->second.last_write;
}

void i2c_test_set_read_response(i2c_port_t port, uint8_t addr,
                                const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(g_i2c_mutex);
    auto& dev = g_devices[port][addr];
    dev.read_response.assign(data, data + len);
}

void i2c_test_reset() {
    std::lock_guard<std::mutex> lock(g_i2c_mutex);
    g_devices.clear();
}
