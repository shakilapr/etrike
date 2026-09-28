// MCP2515 CAN controller driver — SPI interface for High-level CAN bus.
// Architecture: SCK=15, MOSI=16, MISO=17, CS=18, INT=47.

#include "can_driver_mcp2515.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace dumb {
namespace {

constexpr const char* kTag = "mcp2515";

spi_device_handle_t g_spi_handle = nullptr;
static SemaphoreHandle_t g_control_mutex = nullptr;
static std::atomic<TaskHandle_t> g_rx_task_handle{nullptr};

static void IRAM_ATTR mcp_int_isr(void* arg) {
    if (const TaskHandle_t rx_task = g_rx_task_handle.load(std::memory_order_acquire)) {
        BaseType_t yield = pdFALSE;
        vTaskNotifyGiveFromISR(rx_task, &yield);
        if (yield) portYIELD_FROM_ISR(yield);
    }
}

class ControlGuard {
public:
    explicit ControlGuard(uint32_t timeout_ms)
        : locked_(g_control_mutex
                  && xSemaphoreTake(g_control_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {}
    ~ControlGuard() { if (locked_) xSemaphoreGive(g_control_mutex); }
    explicit operator bool() const { return locked_; }
    void release() {
        if (locked_) {
            xSemaphoreGive(g_control_mutex);
            locked_ = false;
        }
    }
private:
    bool locked_;
};

} // anonymous namespace

bool Mcp2515Driver::spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    const esp_err_t result = spi_device_transmit(g_spi_handle, &t);
    if (result != ESP_OK) {
        m_spi_fail_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void Mcp2515Driver::spi_write_byte(uint8_t addr, uint8_t data) {
    uint8_t tx[3] = { kCmdWrite, addr, data };
    (void)spi_transfer(tx, nullptr, 3);
}

uint8_t Mcp2515Driver::spi_read_byte(uint8_t addr) {
    uint8_t tx[3] = { kCmdRead, addr, 0x00 };
    uint8_t rx[3] = {};
    (void)spi_transfer(tx, rx, 3);
    return rx[2];
}

void Mcp2515Driver::reset() {
    uint8_t cmd = kCmdReset;
    (void)spi_transfer(&cmd, nullptr, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

uint8_t Mcp2515Driver::read_reg(uint8_t reg) {
    return spi_read_byte(reg);
}

void Mcp2515Driver::write_reg(uint8_t reg, uint8_t val) {
    spi_write_byte(reg, val);
}

void Mcp2515Driver::modify_reg(uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t tx[4] = { kCmdBitModify, reg, mask, val };
    (void)spi_transfer(tx, nullptr, 4);
}

uint8_t Mcp2515Driver::read_status() {
    uint8_t tx[2] = { kCmdReadStatus, 0x00 };
    uint8_t rx[2] = {};
    (void)spi_transfer(tx, rx, 2);
    return rx[1];
}

bool Mcp2515Driver::spi_read_burst(uint8_t start_addr, uint8_t* data, size_t len) {
    constexpr size_t kMaxBurst = 16;
    uint8_t tx_buf[kMaxBurst] = {};
    uint8_t rx_buf[kMaxBurst] = {};
    tx_buf[0] = kCmdRead;
    tx_buf[1] = start_addr;

    spi_transaction_t t = {};
    t.length    = (2 + len) * 8;
    t.rxlength  = (2 + len) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;
    const esp_err_t result = spi_device_transmit(g_spi_handle, &t);

    if (result != ESP_OK) {
        m_spi_fail_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    memcpy(data, &rx_buf[2], len);
    return true;
}

bool Mcp2515Driver::spi_write_burst(uint8_t start_addr, const uint8_t* data, size_t len) {
    constexpr size_t kMaxBurst = 16;
    if (len + 2 > kMaxBurst) return false;

    uint8_t tx_buf[kMaxBurst] = {};
    tx_buf[0] = kCmdWrite;
    tx_buf[1] = start_addr;
    memcpy(&tx_buf[2], data, len);

    spi_transaction_t t = {};
    t.length    = (2 + len) * 8;
    t.tx_buffer = tx_buf;
    const esp_err_t result = spi_device_transmit(g_spi_handle, &t);
    if (result != ESP_OK) {
        m_spi_fail_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool Mcp2515Driver::read_frame_burst(can::Frame& out, uint8_t base_addr) {
    uint8_t buf[13]{};
    if (!spi_read_burst(base_addr, buf, 13)) return false;

    out = {};
    out.extended = (buf[1] & 0x08) != 0;
    out.id = (uint32_t(buf[0]) << 3) | ((buf[1] >> 5) & 0x07);
    out.dlc = buf[4] & 0x0F;

    for (int i = 0; i < out.dlc && i < 8; ++i) {
        out.data[i] = buf[5 + i];
    }
    return out.dlc <= 8;
}

bool Mcp2515Driver::init_gpio() {
    gpio_set_direction(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_PULLUP_ONLY);

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "GPIO ISR service install failed: %d", err);
        return false;
    }
    err = gpio_set_intr_type(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_INTR_NEGEDGE);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "GPIO interrupt type set failed: %d", err);
        return false;
    }
    err = gpio_isr_handler_add(static_cast<gpio_num_t>(m_cfg.int_gpio), mcp_int_isr, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "GPIO ISR handler add failed: %d", err);
        return false;
    }

    return true;
}

bool Mcp2515Driver::init_spi() {
    if (g_spi_handle) return true;

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = m_cfg.mosi_gpio;
    bus_cfg.miso_io_num     = m_cfg.miso_gpio;
    bus_cfg.sclk_io_num     = m_cfg.sck_gpio;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 64;

    if (spi_bus_initialize(static_cast<spi_host_device_t>(m_cfg.spi_host),
                           &bus_cfg, SPI_DMA_DISABLED) != ESP_OK) {
        ESP_LOGE(kTag, "SPI bus init failed");
        return false;
    }

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode           = 0;
    dev_cfg.clock_speed_hz = m_cfg.spi_freq;
    dev_cfg.spics_io_num   = m_cfg.cs_gpio;
    dev_cfg.queue_size     = 4;

    if (spi_bus_add_device(static_cast<spi_host_device_t>(m_cfg.spi_host),
                           &dev_cfg, &g_spi_handle) != ESP_OK) {
        ESP_LOGE(kTag, "SPI device add failed");
        return false;
    }

    return true;
}

bool Mcp2515Driver::init_mcp2515_regs(bool cold_boot) {
    uint8_t canstat = 0x00;
    const int max_attempts = cold_boot ? 4 : 1;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        reset();
        canstat = read_reg(kRegCanStat);
        if ((canstat >> 5) == 0x04) break;

        if (attempt + 1 < max_attempts) {
            int delay_ms = 200 * (attempt + 1);
            ESP_LOGW(kTag, "MCP2515 not ready (CANSTAT=0x%02X), retrying in %dms...",
                     canstat, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }

    if ((canstat >> 5) != 0x04) {
        ESP_LOGE(kTag, "MCP2515 not in config mode after %d attempts (CANSTAT=0x%02X)",
                 max_attempts, canstat);
        return false;
    }

    write_reg(kRegCnf1, kCnf1_500k);
    write_reg(kRegCnf2, kCnf2_500k);
    write_reg(kRegCnf3, kCnf3_500k);

    write_reg(kRegRxb0Ctrl, 0x64);
    write_reg(kRegRxb1Ctrl, 0x60);
    write_reg(kRegCanIntE, 0xA3);

    Mode start_mode = Mode::Normal;
    uint8_t reqop = static_cast<uint8_t>(start_mode);
    modify_reg(kRegCanCtrl, 0xE0, reqop);
    vTaskDelay(pdMS_TO_TICKS(1));

    canstat = read_reg(kRegCanStat);
    uint8_t opmode = (canstat >> 5) & 0x07;
    uint8_t expected = static_cast<uint8_t>(start_mode) >> 5;
    if (opmode != expected) {
        ESP_LOGE(kTag, "MCP2515 failed to enter normal mode (CANSTAT=0x%02X)", canstat);
        return false;
    }
    m_mode.store(start_mode, std::memory_order_relaxed);
    return true;
}

bool Mcp2515Driver::init() {
    if (!g_control_mutex) g_control_mutex = xSemaphoreCreateMutex();
    if (!g_control_mutex) return false;
    ControlGuard guard(500);
    if (!guard) return false;
    if (!init_gpio()) return false;
    if (!init_spi()) return false;
    if (!init_mcp2515_regs()) return false;

    m_initialized = true;
    ESP_LOGI(kTag, "MCP2515 ready: SCK=%d MOSI=%d MISO=%d CS=%d INT=%d @ %d MHz",
             m_cfg.sck_gpio, m_cfg.mosi_gpio, m_cfg.miso_gpio,
             m_cfg.cs_gpio, m_cfg.int_gpio, m_cfg.spi_freq / 1'000'000);
    return true;
}

bool Mcp2515Driver::recover() {
    if (!is_initialized()) return false;
    bool expected = false;
    if (!m_recovering.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    const uint32_t attempt = m_recovery_attempts.fetch_add(1, std::memory_order_relaxed) + 1;
    ESP_LOGW(kTag, "state=bus_off recovery=start attempt=%lu",
             static_cast<unsigned long>(attempt));
    bool ok = false;
    {
        ControlGuard guard(200);
        if (guard) {
            m_has_pending = false;
            ok = init_mcp2515_regs(false);
        }
    }
    if (ok) {
        m_bus_off.store(false, std::memory_order_release);
        m_first_rx_pending.store(true, std::memory_order_release);
        m_first_tx_pending.store(true, std::memory_order_release);
        const int64_t started = m_bus_off_started_us.load(std::memory_order_relaxed);
        ESP_LOGI(kTag, "state=active recovery=complete elapsed_ms=%lld",
                 static_cast<long long>((esp_timer_get_time() - started) / 1000));
    } else {
        m_recovery_failures.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGE(kTag, "recovery=failed attempt=%lu", static_cast<unsigned long>(attempt));
    }
    m_recovering.store(false, std::memory_order_release);
    return ok;
}

bool Mcp2515Driver::set_mode(Mode mode) {
    if (!m_initialized) return false;
    ControlGuard guard(10);
    if (!guard || is_recovering()) return false;
    modify_reg(kRegCanCtrl, 0xE0, static_cast<uint8_t>(mode));
    vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t canstat = read_reg(kRegCanStat);
    uint8_t opmode = (canstat >> 5) & 0x07;
    uint8_t expected = static_cast<uint8_t>(mode) >> 5;
    if (opmode != expected) return false;
    m_mode.store(mode, std::memory_order_relaxed);
    return true;
}

void Mcp2515Driver::set_rx_task_handle(TaskHandle_t handle) {
    g_rx_task_handle.store(handle, std::memory_order_release);
}

bool Mcp2515Driver::send(const can::Frame& frame, uint32_t timeout_ms) {
    if (!m_initialized) return false;
    ControlGuard guard(timeout_ms + 2);
    if (!guard || is_recovering() || bus_off()) return false;
    if (m_mode.load(std::memory_order_relaxed) == Mode::ListenOnly) {
        return false;
    }

    bool is_estop = (frame.id == 0x001);
    bool is_telem = (frame.id == 0x210 || frame.id == 0x310 || frame.id == 0x311 || frame.id == 0x220 || frame.id == 0x620 || frame.id == 0x121);
    uint8_t txb_data_reg = is_estop ? kRegTxb2Data : (is_telem ? kRegTxb1Data : kRegTxb0Data);
    uint8_t rts_cmd      = is_estop ? kCmdRtsTx2 : (is_telem ? kCmdRtsTx1 : kCmdRtsTx0);
    uint8_t txreq_bit    = is_estop ? kReadStatusTx2Req : (is_telem ? kReadStatusTx1Req : kReadStatusTx0Req);

    uint8_t status = read_status();
    if (status & txreq_bit) {
        if (!is_estop) {
            const uint8_t alt_txreq = is_telem ? kReadStatusTx0Req : kReadStatusTx1Req;
            if (!(status & alt_txreq)) {
                txb_data_reg = is_telem ? kRegTxb0Data : kRegTxb1Data;
                rts_cmd      = is_telem ? kCmdRtsTx0 : kCmdRtsTx1;
                txreq_bit    = alt_txreq;
            }
        }
    }

    if (status & txreq_bit) {
        int64_t deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;
        while (read_status() & txreq_bit) {
            if (is_recovering() || bus_off() || esp_timer_get_time() > deadline) return false;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    uint8_t dlc = (frame.id == 0x001u) ? 0 : std::min<uint8_t>(frame.dlc, 8);
    uint8_t tx_buf[13] = {};

    uint8_t sidh = (frame.id >> 3) & 0xFF;
    uint8_t sidl = (frame.id & 0x07) << 5;
    if (frame.extended) sidl |= 0x08;
    tx_buf[0] = sidh;
    tx_buf[1] = sidl;
    tx_buf[4] = dlc & 0x0F;
    for (int i = 0; i < dlc; ++i) tx_buf[5 + i] = frame.data[i];
    if (!spi_write_burst(txb_data_reg, tx_buf, 5 + dlc)) return false;

    uint8_t rts = rts_cmd;
    if (!spi_transfer(&rts, nullptr, 1)) return false;

    guard.release();
    log_first_io_after_recovery(false);
    return true;
}

bool Mcp2515Driver::receive(can::Frame& out, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    {
        ControlGuard guard(5);
        if (!guard || is_recovering()) return false;
        if (m_has_pending) {
            out = m_pending_frame;
            m_has_pending = false;
            guard.release();
            log_first_io_after_recovery(true);
            return true;
        }
    }

    int64_t const deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;

    while (true) {
        ControlGuard guard(5);
        if (!guard || is_recovering()) return false;
        uint8_t canintf = read_reg(kRegCanIntF);

        if (canintf & 0xA0) {
            uint8_t eflg = read_reg(kRegEflg);
            if (eflg & 0x20) { // Bit 5: TXBO = Bus-Off (TEC > 255)
                const bool was_bus_off = m_bus_off.exchange(true, std::memory_order_acq_rel);
                if (!was_bus_off) {
                    m_bus_off_started_us.store(esp_timer_get_time(), std::memory_order_relaxed);
                    ESP_LOGW(kTag, "MCP2515 entered bus-off (TEC=%d REC=%d)",
                             read_reg(kRegTec), read_reg(kRegRec));
                }
            } else if (eflg & 0x18) { // Bits 4 & 3: TXEP or RXEP = Error-Passive
                ESP_LOGW(kTag, "MCP2515 error-passive (TEC=%d REC=%d)",
                         read_reg(kRegTec), read_reg(kRegRec));
            }
            if (eflg & 0xC0) { // Bits 7 & 6: RX1OVR & RX0OVR (Buffer Overflow)
                modify_reg(kRegEflg, 0xC0, 0x00); // Clear overflow flags in EFLG
            }
            modify_reg(kRegCanIntF, 0xA0, 0x00);
        }

        if (canintf & 0x01) {
            if (!read_frame_burst(out, kRegRxb0Data)) return false;
            modify_reg(kRegCanIntF, 0x01, 0x00);

            canintf = read_reg(kRegCanIntF);
            if (canintf & 0x02) {
                if (!read_frame_burst(m_pending_frame, kRegRxb1Data)) return false;
                modify_reg(kRegCanIntF, 0x02, 0x00);
                m_has_pending = true;
            }
            guard.release();
            log_first_io_after_recovery(true);
            return true;
        }

        if (canintf & 0x02) {
            if (!read_frame_burst(out, kRegRxb1Data)) return false;
            modify_reg(kRegCanIntF, 0x02, 0x00);
            guard.release();
            log_first_io_after_recovery(true);
            return true;
        }

        if (esp_timer_get_time() > deadline) return false;

        guard.release();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    }
}

void Mcp2515Driver::get_error_counters(uint8_t& tec, uint8_t& rec) {
    ControlGuard guard(5);
    if (!guard || is_recovering()) { tec = rec = 0; return; }
    tec = read_reg(kRegTec);
    rec = read_reg(kRegRec);
}

bool Mcp2515Driver::health_probe() {
    if (!m_initialized) return false;
    ControlGuard guard(5);
    if (!guard || is_recovering()) return true;
    const uint8_t cnf2 = read_reg(kRegCnf2);
    const uint8_t cnf3 = read_reg(kRegCnf3);
    const bool ok = cnf2 == kCnf2_500k && cnf3 == kCnf3_500k;
    if (!ok) {
        static int64_t last_log_us = 0;
        const int64_t now = esp_timer_get_time();
        if (now - last_log_us > 5'000'000) {
            last_log_us = now;
            ESP_LOGW(kTag, "health_probe mismatch: CNF2=0x%02X CNF3=0x%02X spi_fail=%lu",
                     cnf2, cnf3,
                     static_cast<unsigned long>(m_spi_fail_count.load(std::memory_order_relaxed)));
        }
    }
    return ok;
}

bool Mcp2515Driver::read_bus_diag(uint8_t& eflg, uint8_t& tec, uint8_t& rec) {
    eflg = tec = rec = 0;
    if (!is_initialized()) return false;
    ControlGuard guard(5);
    if (!guard || is_recovering()) return false;
    eflg = read_reg(kRegEflg);
    tec  = read_reg(kRegTec);
    rec  = read_reg(kRegRec);
    return true;
}

void Mcp2515Driver::log_first_io_after_recovery(bool rx) {
    auto& pending = rx ? m_first_rx_pending : m_first_tx_pending;
    if (!pending.exchange(false, std::memory_order_acq_rel)) return;
    const int64_t started = m_bus_off_started_us.load(std::memory_order_relaxed);
    ESP_LOGI(kTag, "post_recovery first_%s elapsed_ms=%lld", rx ? "rx" : "tx",
             static_cast<long long>((esp_timer_get_time() - started) / 1000));
    if (!m_first_rx_pending.load(std::memory_order_relaxed)
        && !m_first_tx_pending.load(std::memory_order_relaxed)) {
        m_bus_off_started_us.store(0, std::memory_order_relaxed);
    }
}

} // namespace dumb
