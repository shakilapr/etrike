// MCP2515 CAN controller driver — SPI interface for high-level CAN bus.
// Architecture.md §7.2. ESP-IDF SPI master + GPIO.

#include "can_driver_mcp2515.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rt {
namespace {

constexpr const char* kTag = "mcp2515";

// ── CNF timing for 500 kbit/s with 8 MHz crystal ──────────────────
// TQ = 2 * (BRP+1) / Fosc. BRP=1 → TQ=0.25µs.
// 500 kbit/s → bit time = 2µs = 8 TQ.
// PropSeg=2, PS1=3, PS2=2, SJW=1 → total = 1+2+3+2 = 8 TQ.
constexpr uint8_t kCnf1_500k = 0x01;  // SJW=1, BRP=1
constexpr uint8_t kCnf2_500k = 0x92;  // BTLMODE=1, PS2=2, PropSeg=2
constexpr uint8_t kCnf3_500k = 0x02;  // PS1=3, wake filter off

spi_device_handle_t g_spi_handle = nullptr;

}  // anonymous namespace

// ── SPI primitives ─────────────────────────────────────────────────

void Mcp2515Driver::spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_transmit(g_spi_handle, &t);
}

void Mcp2515Driver::spi_write_byte(uint8_t addr, uint8_t data) {
    uint8_t tx[3] = { kCmdWrite, addr, data };
    spi_transfer(tx, nullptr, 3);
}

uint8_t Mcp2515Driver::spi_read_byte(uint8_t addr) {
    uint8_t tx[3] = { kCmdRead, addr, 0x00 };
    uint8_t rx[3] = {};
    spi_transfer(tx, rx, 3);
    return rx[2];
}

void Mcp2515Driver::spi_write_buf(uint8_t addr, const uint8_t* data, size_t len) {
    // For load TX buffer: first byte is command+addr, then data
    uint8_t cmd = addr;  // kCmdLoadTx0 already includes the command bits
    uint8_t tx_hdr[1] = { cmd };
    spi_transaction_t t1 = {};
    t1.length = 8;
    t1.tx_buffer = tx_hdr;
    spi_device_transmit(g_spi_handle, &t1);

    spi_transaction_t t2 = {};
    t2.length = len * 8;
    t2.tx_buffer = data;
    spi_device_transmit(g_spi_handle, &t2);
}

void Mcp2515Driver::spi_read_buf(uint8_t addr, uint8_t* data, size_t len) {
    uint8_t tx_hdr[1] = { addr };
    spi_transaction_t t1 = {};
    t1.length = 8;
    t1.tx_buffer = tx_hdr;
    spi_device_transmit(g_spi_handle, &t1);

    spi_transaction_t t2 = {};
    t2.rxlength = len * 8;
    t2.rx_buffer = data;
    spi_device_transmit(g_spi_handle, &t2);
}

// ── MCP2515 register primitives ────────────────────────────────────

void Mcp2515Driver::reset() {
    uint8_t cmd = kCmdReset;
    spi_transfer(&cmd, nullptr, 1);
    vTaskDelay(pdMS_TO_TICKS(10));  // post-reset stabilization
}

uint8_t Mcp2515Driver::read_reg(uint8_t reg) {
    return spi_read_byte(reg);
}

void Mcp2515Driver::write_reg(uint8_t reg, uint8_t val) {
    spi_write_byte(reg, val);
}

void Mcp2515Driver::modify_reg(uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t tx[4] = { kCmdBitModify, reg, mask, val };
    spi_transfer(tx, nullptr, 4);
}

uint8_t Mcp2515Driver::read_status() {
    uint8_t tx[2] = { kCmdReadStatus, 0x00 };
    uint8_t rx[2] = {};
    spi_transfer(tx, rx, 2);
    return rx[1];
}

// ── Init ───────────────────────────────────────────────────────────

bool Mcp2515Driver::init() {
    // ── GPIO: CS and INT ──────────────────────────────────────────
    gpio_set_direction(static_cast<gpio_num_t>(m_cfg.cs_gpio), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(m_cfg.cs_gpio), 1);

    gpio_set_direction(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_PULLUP_ONLY);

    // ── SPI bus ───────────────────────────────────────────────────
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
    dev_cfg.mode           = 0;          // CPOL=0, CPHA=0
    dev_cfg.clock_speed_hz = m_cfg.spi_freq;
    dev_cfg.spics_io_num   = m_cfg.cs_gpio;
    dev_cfg.queue_size     = 4;

    if (spi_bus_add_device(static_cast<spi_host_device_t>(m_cfg.spi_host),
                           &dev_cfg, &g_spi_handle) != ESP_OK) {
        ESP_LOGE(kTag, "SPI device add failed");
        return false;
    }

    // ── MCP2515 reset ─────────────────────────────────────────────
    reset();

    // Verify device: read CANSTAT after reset → should be 0x80 (config mode)
    uint8_t canstat = read_reg(kRegCanStat);
    if ((canstat >> 5) != 0x04) {  // OPMOD bits = 100 = config mode
        ESP_LOGE(kTag, "MCP2515 not in config mode after reset (CANSTAT=0x%02X)", canstat);
        return false;
    }

    // ── Configure bitrate: 500 kbit/s ─────────────────────────────
    write_reg(kRegCnf1, kCnf1_500k);
    write_reg(kRegCnf2, kCnf2_500k);
    write_reg(kRegCnf3, kCnf3_500k);

    // ── RX buffers: accept all, no filters ────────────────────────
    // RXB0: receive all
    write_reg(kRegRxb0Ctrl, 0x60);  // RXM[1:0]=11 = turn mask/filters off; receive all
    // RXB1: receive all
    write_reg(kRegRxb1Ctrl, 0x60);

    // ── Interrupts: enable RX0BF + RX1BF + error flags ────────────
    write_reg(kRegCanIntE, 0x03);  // RX0IF + RX1IF

    // ── Normal mode ────────────────────────────────────────────────
    modify_reg(kRegCanCtrl, 0xE0, 0x00);  // REQOP[2:0]=000 = normal mode
    vTaskDelay(pdMS_TO_TICKS(1));

    canstat = read_reg(kRegCanStat);
    uint8_t opmode = (canstat >> 5) & 0x07;
    if (opmode != 0x00) {
        ESP_LOGE(kTag, "MCP2515 failed to enter normal mode (CANSTAT=0x%02X)", canstat);
        return false;
    }

    m_initialized = true;
    ESP_LOGI(kTag, "MCP2515 ready: SCK=%d MOSI=%d MISO=%d CS=%d INT=%d @ %d MHz",
             m_cfg.sck_gpio, m_cfg.mosi_gpio, m_cfg.miso_gpio,
             m_cfg.cs_gpio, m_cfg.int_gpio, m_cfg.spi_freq / 1'000'000);
    return true;
}

// ── Send ───────────────────────────────────────────────────────────

bool Mcp2515Driver::send(const can::Frame& frame, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    // Check TXB0 free via TX0IF
    uint8_t status = read_status();
    if (!(status & 0x04)) {  // TX0IF clear → buffer busy, wait for TX0IF
        // Wait for buffer to become free
        int64_t deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;
        while (!(read_status() & 0x04)) {
            if (esp_timer_get_time() > deadline) return false;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Load TX buffer 0
    // TXB0CTRL: TXP[1:0]=00 (highest priority)
    write_reg(kRegTxb0Ctrl, 0x03);  // TXP=11 (highest priority)

    // Standard ID (11-bit) → SIDH+SIDL
    uint8_t sidh = (frame.id >> 3) & 0xFF;
    uint8_t sidl = (frame.id & 0x07) << 5;
    if (frame.extended) sidl |= 0x08;  // EXIDE
    write_reg(0x31, sidh);  // TXB0SIDH
    write_reg(0x32, sidl);  // TXB0SIDL

    // DLC
    write_reg(0x35, frame.dlc & 0x0F);  // TXB0DLC

    // Data
    for (int i = 0; i < frame.dlc && i < 8; ++i) {
        write_reg(0x36 + i, frame.data[i]);  // TXB0D0..D7
    }

    // Request send
    uint8_t rts = kCmdRtsTx0;
    spi_transfer(&rts, nullptr, 1);

    return true;
}

// ── Receive ────────────────────────────────────────────────────────

bool Mcp2515Driver::receive(can::Frame& out, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    int64_t deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;

    while (true) {
        uint8_t status = read_status();

        // Check RXB0 or RXB1
        bool rx0 = (status & 0x01) != 0;  // RX0IF
        bool rx1 = (status & 0x02) != 0;  // RX1IF

        uint8_t base = 0;
        if (rx0) {
            base = 0x61;  // RXB0SIDH
        } else if (rx1) {
            base = 0x71;  // RXB1SIDH
        } else {
            // No frame available
            if (esp_timer_get_time() > deadline) return false;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // Read SID
        uint8_t sidh = read_reg(base);      // SIDH
        uint8_t sidl = read_reg(base + 1);  // SIDL
        out.extended = (sidl & 0x08) != 0;
        out.id = (uint32_t(sidh) << 3) | ((sidl >> 5) & 0x07);

        // Read DLC
        out.dlc = read_reg(base + 4) & 0x0F;

        // Read data
        for (int i = 0; i < out.dlc && i < 8; ++i) {
            out.data[i] = read_reg(base + 5 + i);
        }

        // Clear interrupt flag
        if (rx0) {
            modify_reg(kRegCanIntF, 0x01, 0x00);  // clear RX0IF
        } else {
            modify_reg(kRegCanIntF, 0x02, 0x00);  // clear RX1IF
        }

        return true;
    }
}

// ── Diagnostics ────────────────────────────────────────────────────

void Mcp2515Driver::get_error_counters(uint8_t& tec, uint8_t& rec) {
    tec = read_reg(kRegTec);
    rec = read_reg(kRegRec);
}

}  // namespace rt
