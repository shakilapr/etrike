// MCP2515 CAN controller driver — SPI interface for high-level CAN bus.
// Architecture.md §7.2. ESP-IDF SPI master + GPIO.

#include "can_driver_mcp2515.h"
#include <algorithm>
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
// TQ = 2 * (BRP+1) / Fosc. BRP=0 → TQ=0.25µs.
// 500 kbit/s → bit time = 2µs = 8 TQ.
// Sync=1, PropSeg=2, PS1=3, PS2=2, SJW=1 → total = 8 TQ.
constexpr uint8_t kCnf1_500k = 0x00;  // SJW=1, BRP=0
constexpr uint8_t kCnf2_500k = 0x91;  // BTLMODE=1, PS1=3, PropSeg=2
constexpr uint8_t kCnf3_500k = 0x01;  // PS2=2 (PHSEG2=1 → 1+1=2 TQ, total 8 TQ)

spi_device_handle_t g_spi_handle = nullptr;

// ── ISR notification infrastructure ──────────────────────────────
// The GPIO ISR on the MCP2515 INT pin (GPIO 40) notifies the RX
// task when a CAN frame is available. See docs/latency-issues.md §3.
// Task handle is set on first receive() call; null-guarded in ISR
// for the cold-boot window between init() and task creation.
static TaskHandle_t g_rx_task_handle = nullptr;

// GPIO ISR for MCP2515 INT pin (falling edge, active-low).
// IRAM_ATTR: prevents crash during concurrent flash operations
// (NVS writes, OTA, crash dumps). Both vTaskNotifyGiveFromISR and
// portYIELD_FROM_ISR are already in IRAM under ESP-IDF defaults.
static void IRAM_ATTR mcp_int_isr(void* arg) {
    if (g_rx_task_handle) {
        BaseType_t yield = pdFALSE;
        vTaskNotifyGiveFromISR(g_rx_task_handle, &yield);
        if (yield) portYIELD_FROM_ISR(yield);
    }
}

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

// ── Burst SPI read ───────────────────────────────────────────────
// Reads 'len' consecutive bytes from the MCP2515 starting at
// 'start_addr' in a single spi_device_transmit() call. The MCP2515
// auto-increments its internal address register for burst reads.
//
// SPI bus contention note: ESP-IDF's SPI master serializes individual
// spi_device_transmit() calls via a per-device queue spinlock. Since
// this is a single transaction, there is no interleaving risk with
// tx_high (prio 3) or get_error_counters() from t_control (prio 4).
// If multi-transaction sequences are ever added, wrap them in
// spi_device_acquire_bus() / spi_device_release_bus().
void Mcp2515Driver::spi_read_burst(uint8_t start_addr, uint8_t* data, size_t len) {
    constexpr size_t kMaxBurst = 16;  // RX buffer is 13 bytes
    uint8_t tx_buf[kMaxBurst] = {};
    uint8_t rx_buf[kMaxBurst] = {};
    tx_buf[0] = kCmdRead;       // 0x03
    tx_buf[1] = start_addr;

    spi_transaction_t t = {};
    t.length    = (2 + len) * 8;
    t.rxlength  = (2 + len) * 8;
    t.tx_buffer = tx_buf;
    t.rx_buffer = rx_buf;
    spi_device_transmit(g_spi_handle, &t);

    memcpy(data, &rx_buf[2], len);
}

// ── Burst frame read ─────────────────────────────────────────────
// Reads a complete CAN frame from an MCP2515 RX buffer in one SPI
// transaction. 13 bytes: SIDH, SIDL, EID8, EID0, DLC, D0-D7.
void Mcp2515Driver::read_frame_burst(can::Frame& out, uint8_t base_addr) {
    uint8_t buf[13];
    spi_read_burst(base_addr, buf, 13);

    // Standard ID (11-bit): SIDH[7:0] = ID[10:3], SIDL[7:5] = ID[2:0]
    out.extended = (buf[1] & 0x08) != 0;  // EXIDE bit
    out.id = (uint32_t(buf[0]) << 3) | ((buf[1] >> 5) & 0x07);

    // DLC
    out.dlc = buf[4] & 0x0F;

    // Data bytes
    for (int i = 0; i < out.dlc && i < 8; ++i) {
        out.data[i] = buf[5 + i];
    }
}

// ── Init ───────────────────────────────────────────────────────────

// ── Init sub-steps (Part 4) ─────────────────────────────────────────

bool Mcp2515Driver::init_gpio() {
    // CS pin is managed by the SPI driver via spics_io_num — do NOT
    // configure it as a manual GPIO, which would conflict.
    gpio_set_direction(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_PULLUP_ONLY);

    // ── Install ISR for interrupt-driven RX ───────────────────────
    // ESP_INTR_FLAG_LEVEL3: default GPIO interrupt level on ESP32-S3.
    // gpio_install_isr_service is idempotent — second call returns
    // ESP_ERR_INVALID_STATE, safe for bus-off re-init (main.cpp:497).
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    gpio_set_intr_type(static_cast<gpio_num_t>(m_cfg.int_gpio), GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(static_cast<gpio_num_t>(m_cfg.int_gpio), mcp_int_isr, nullptr);

    return true;
}

bool Mcp2515Driver::init_spi() {
    // Idempotent: skip re-init if SPI bus is already running (bus-off recovery path).
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
    dev_cfg.mode           = 0;          // CPOL=0, CPHA=0
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

bool Mcp2515Driver::init_mcp2515_regs() {
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
    write_reg(kRegRxb0Ctrl, 0x60);  // RXM[1:0]=11 = turn mask/filters off
    write_reg(kRegRxb1Ctrl, 0x60);

    // ── Interrupts: enable RX0BF + RX1BF ──────────────────────────
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
    return true;
}

// ── Init (orchestrator) ─────────────────────────────────────────────

bool Mcp2515Driver::init() {
    if (!init_gpio()) return false;
    if (!init_spi()) return false;
    if (!init_mcp2515_regs()) return false;

    m_initialized = true;
    ESP_LOGI(kTag, "MCP2515 ready: SCK=%d MOSI=%d MISO=%d CS=%d INT=%d @ %d MHz",
             m_cfg.sck_gpio, m_cfg.mosi_gpio, m_cfg.miso_gpio,
             m_cfg.cs_gpio, m_cfg.int_gpio, m_cfg.spi_freq / 1'000'000);
    return true;
}

// ── Send ───────────────────────────────────────────────────────────

bool Mcp2515Driver::send(const can::Frame& frame, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    // Check TXB0 free via TX0REQ (bit 0) in Read Status byte.
    // MCP2515 datasheet: bit 0=TX0REQ (TX request pending), bit 2=TX0IF (complete).
    // Only wait if a prior transmission is still in progress (TX0REQ=1).
    // After reset TX0REQ=0, so first send proceeds immediately.
    uint8_t status = read_status();
    if (status & 0x01) {  // TX0REQ=1 → buffer busy, wait for completion
        int64_t deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;
        while (read_status() & 0x01) {
            if (esp_timer_get_time() > deadline) return false;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Load TX buffer 0
    // TXP defaults to 11 (highest priority) after reset — no explicit write needed.

    // Standard ID (11-bit) → SIDH+SIDL
    uint8_t sidh = (frame.id >> 3) & 0xFF;
    uint8_t sidl = (frame.id & 0x07) << 5;
    if (frame.extended) sidl |= 0x08;  // EXIDE
    write_reg(kRegTxb0Data, sidh);   // TXB0SIDH
    write_reg(kRegTxb0Data1, sidl);  // TXB0SIDL

    // DLC
    write_reg(kRegTxb0Dlc, frame.dlc & 0x0F);  // TXB0DLC

    // Data
    for (int i = 0; i < frame.dlc && i < 8; ++i) {
        write_reg(kRegTxb0D0 + i, frame.data[i]);  // TXB0D0..D7
    }

    // Request send
    uint8_t rts = kCmdRtsTx0;
    spi_transfer(&rts, nullptr, 1);

    return true;
}

// ── Receive (ISR-driven, with polling fallback) ───────────────────

bool Mcp2515Driver::receive(can::Frame& out, uint32_t timeout_ms) {
    if (!m_initialized) return false;

    // ── Register this task on first call ──────────────────────────
    // Cold boot: ISR is installed during init() but task doesn't
    // exist yet. Bus-off recovery: task already exists and handle
    // is already set (idempotent assignment).
    if (g_rx_task_handle == nullptr)
        g_rx_task_handle = xTaskGetCurrentTaskHandle();

    // ── Return pre-read second buffer with zero SPI latency ───────
    // When both RXB0 and RXB1 have frames, the first is returned
    // immediately and the second is cached here. The INT pin stays
    // low while any interrupt flag is set, so no new ISR edge fires
    // for RXB1 — we must drain it proactively.
    if (m_has_pending) {
        out = m_pending_frame;
        m_has_pending = false;
        return true;
    }

    int64_t const deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;

    while (true) {
        // ── Check CANINTF for pending RX buffers ──────────────────
        uint8_t canintf = read_reg(kRegCanIntF);

        if (canintf & 0x01) {  // RX0IF — RXB0 has data
            read_frame_burst(out, kRegRxb0Data);
            modify_reg(kRegCanIntF, 0x01, 0x00);  // clear RX0IF

            // Check for second frame in RXB1 (no new ISR edge —
            // INT stays low while any flag remains set)
            canintf = read_reg(kRegCanIntF);
            if (canintf & 0x02) {
                read_frame_burst(m_pending_frame, kRegRxb1Data);
                modify_reg(kRegCanIntF, 0x02, 0x00);  // clear RX1IF
                m_has_pending = true;
            }
            return true;
        }

        if (canintf & 0x02) {  // RX1IF — RXB1 has data
            read_frame_burst(out, kRegRxb1Data);
            modify_reg(kRegCanIntF, 0x02, 0x00);  // clear RX1IF
            return true;
        }

        // ── No frame available ────────────────────────────────────
        if (esp_timer_get_time() > deadline) return false;

        // ISR-driven wait: MCP2515 INT pin (GPIO 40) fires on RX buffer fill.
        // ulTaskNotifyTake blocks until the ISR gives a notification or 1ms
        // timeout expires (silent bus fallback). Combined ISR+polling approach:
        // the ISR is the fast path (microsecond latency); the 1ms timeout
        // handles the silent-bus case without burning CPU on SPI reads.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
    }
}

// ── Diagnostics ────────────────────────────────────────────────────

void Mcp2515Driver::get_error_counters(uint8_t& tec, uint8_t& rec) {
    tec = read_reg(kRegTec);
    rec = read_reg(kRegRec);
}

}  // namespace rt
