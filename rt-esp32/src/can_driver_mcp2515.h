#pragma once
// MCP2515 CAN controller via SPI — high-level CAN bus.
// Architecture.md §7.2: SCK=36, MOSI=37, MISO=38, CS=39, INT=40.
// 10 MHz SPI, 500 kbit/s CAN.

#include <atomic>
#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol/compat/can.hpp"
#include "config.h"

namespace rt {

class Mcp2515Driver {
public:
    struct Config {
        int sck_gpio;
        int mosi_gpio;
        int miso_gpio;
        int cs_gpio;
        int int_gpio;
        int spi_host;
        int spi_freq;
    };

    static Config default_config() {
        return { rt::kSpiSckGpio, rt::kSpiMosiGpio, rt::kSpiMisoGpio,
                 rt::kSpiCsGpio, rt::kMcpIntGpio, 2, 8'000'000 };
    }

    Mcp2515Driver() : m_cfg(default_config()) {}

    Mcp2515Driver(const Mcp2515Driver&) = delete;
    Mcp2515Driver& operator=(const Mcp2515Driver&) = delete;

    // ── Operating mode (for diagnostics) ────────────────────────────
    enum class Mode : uint8_t {
        Normal       = 0x00,  // REQOP=000 — normal TX/RX
        Sleep        = 0x20,  // REQOP=001 — low power, wakes on CAN activity
        Loopback     = 0x40,  // REQOP=010 — internal TX→RX, bus not driven
        ListenOnly   = 0x60,  // REQOP=011 — RX only, never ACKs, bus-safe
        Configuration = 0x80, // REQOP=100 — required for register writes
    };

    // ── Lifecycle ─────────────────────────────────────────────────

    bool init();
    bool recover();
    bool is_initialized() const { return m_initialized.load(std::memory_order_relaxed); }
    bool is_recovering() const { return m_recovering.load(std::memory_order_acquire); }
    Mode operating_mode() const { return m_mode.load(std::memory_order_relaxed); }
    bool can_transmit() const {
        return is_initialized() && !is_recovering() && !bus_off()
            && operating_mode() != Mode::ListenOnly;
    }

    /// Switch operating mode. Returns false if the chip fails to enter
    /// the requested mode within 1ms. ListenOnly is safe for bus monitoring.
    bool set_mode(Mode mode);

    /// Set FreeRTOS task handle for ISR wakeups.
    void set_rx_task_handle(TaskHandle_t handle);

    // ── Frame I/O ──────────────────────────────────────────────────

    bool send(const can::Frame& frame, uint32_t timeout_ms = 2);
    bool receive(can::Frame& out, uint32_t timeout_ms = 100);

    // ── Diagnostics ────────────────────────────────────────────────

    void get_error_counters(uint8_t& tec, uint8_t& rec);
    /// Read EFLG/TEC/REC in one guard window (3 register reads, 1 Hz cadence).
    /// Returns false (values zeroed) when uninitialized, recovering, or the
    /// control mutex is contended. EFLG bits: 7=TXBO 6=TXEP 5=RXEP 4=TXWAR
    /// 3=RXWAR 2=EWARN 1=TXERR 0=RXERR.
    bool read_bus_diag(uint8_t& eflg, uint8_t& tec, uint8_t& rec);
    /// Monotonic count of failed SPI transactions (mutex timeout or
    /// spi_device_transmit error) since init. RT-exclusive transport health.
    uint32_t spi_failure_count() const { return m_spi_fail_count.load(std::memory_order_relaxed); }
    /// Liveness probe: read back two static configuration registers (CNF2/CNF3)
    /// written at init. A silent MCP2515/SPI (power loss, dead MISO) reads back
    /// 0x00/0xFF and fails, whereas a healthy idle chip still returns them.
    /// Used to detect a dead High bus that never latches bus-off. Returns true
    /// (cannot assess) while uninitialized/recovering or on mutex contention.
    bool health_probe();
    bool bus_off() const { return m_bus_off.load(std::memory_order_relaxed); }
    uint32_t recovery_attempts() const { return m_recovery_attempts.load(std::memory_order_relaxed); }
    uint32_t recovery_failures() const { return m_recovery_failures.load(std::memory_order_relaxed); }

    // ── RX overflow telemetry ─────────────────────────────────────
    uint16_t rx_overflow_count() const { return m_rx_overflow_count.load(std::memory_order_relaxed); }
    void record_rx_overflow() { m_rx_overflow_count.fetch_add(1, std::memory_order_relaxed); }

private:
    // ── SPI primitives (ESP-IDF) ───────────────────────────────────
    void spi_write_byte(uint8_t addr, uint8_t data);
    uint8_t spi_read_byte(uint8_t addr);
    bool spi_read_burst(uint8_t start_addr, uint8_t* data, size_t len);
    bool spi_write_burst(uint8_t start_addr, const uint8_t* data, size_t len);
    bool spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len);

    // ── MCP2515 register-level helpers ─────────────────────────────
    void reset();
    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);
    void modify_reg(uint8_t reg, uint8_t mask, uint8_t val);
    uint8_t read_status();
    bool read_frame_burst(can::Frame& out, uint8_t base_addr);
    void log_first_io_after_recovery(bool rx);

    // ── MCP2515 register addresses ─────────────────────────────────
public:
    // CNF timing for 500 kbit/s with 8 MHz crystal
#ifdef MCP2515_16MHZ
    static constexpr uint8_t kCnf1_500k = 0x01;  // SJW=1, BRP=1 (TQ=250ns at 16 MHz)
#else
    static constexpr uint8_t kCnf1_500k = 0x00;  // SJW=1, BRP=0 (TQ=250ns at 8 MHz)
#endif
    static constexpr uint8_t kCnf2_500k = 0x91;  // BTLMODE=1, PS1=3, PropSeg=2
    static constexpr uint8_t kCnf3_500k = 0x01;  // PHSEG2=1 → 2 TQ, total 8 TQ at 500 kbit/s

private:
    static constexpr uint8_t kCmdReset      = 0xC0;
    static constexpr uint8_t kCmdRead       = 0x03;
    static constexpr uint8_t kCmdWrite      = 0x02;
    static constexpr uint8_t kCmdReadStatus = 0xA0;
    static constexpr uint8_t kCmdBitModify  = 0x05;
    static constexpr uint8_t kCmdRtsTx0     = 0x81;
    static constexpr uint8_t kCmdRtsTx1     = 0x82;
    static constexpr uint8_t kCmdRtsTx2     = 0x84;

    static constexpr uint8_t kRegCnf1 = 0x2A;
    static constexpr uint8_t kRegCnf2 = 0x29;
    static constexpr uint8_t kRegCnf3 = 0x28;
    static constexpr uint8_t kRegCanCtrl  = 0x0F;
    static constexpr uint8_t kRegCanStat  = 0x0E;
    static constexpr uint8_t kRegTxb0Ctrl = 0x30;
    static constexpr uint8_t kRegRxb0Ctrl = 0x60;
    static constexpr uint8_t kRegRxb1Ctrl = 0x70;
    static constexpr uint8_t kRegCanIntE  = 0x2B;
    static constexpr uint8_t kRegCanIntF  = 0x2C;
    static constexpr uint8_t kRegEflg     = 0x2D;
    static constexpr uint8_t kRegTec      = 0x1C;
    static constexpr uint8_t kRegRec      = 0x1D;
    // TX / RX buffer register addresses (Part 5 — named constants)
    static constexpr uint8_t kRegTxb0Data  = 0x31;  // TXB0SIDH
    static constexpr uint8_t kRegTxb0Data1 = 0x32;  // TXB0SIDL
    static constexpr uint8_t kRegTxb0Dlc  = 0x35;  // TXB0DLC
    static constexpr uint8_t kRegTxb0D0   = 0x36;  // TXB0D0
    static constexpr uint8_t kRegTxb1Data = 0x41;  // TXB1SIDH
    static constexpr uint8_t kRegTxb2Data = 0x51;  // TXB2SIDH
    static constexpr uint8_t kRegRxb0Data  = 0x61;  // RXB0SIDH
    static constexpr uint8_t kRegRxb1Data  = 0x71;  // RXB1SIDH

public:
    // READ STATUS (0xA0) response bits — MCP2515 datasheet §12.3 / Table 12-2:
    //   RX0IF   = bit 0 (0x01) — RXB0 full interrupt flag
    //   TX0IF   = bit 1 (0x02) — TXB0 empty interrupt flag
    //   TX0REQ  = bit 2 (0x04) — TXB0 pending request
    //   TX1IF   = bit 3 (0x08) — TXB1 empty interrupt flag
    //   TX1REQ  = bit 4 (0x10) — TXB1 pending request
    //   TX2IF   = bit 5 (0x20) — TXB2 empty interrupt flag
    //   TX2REQ  = bit 6 (0x40) — TXB2 pending request
    //   CANINTF = bit 7 (0x80) — Any interrupt pending
    static constexpr uint8_t kReadStatusRx0If   = 0x01;
    static constexpr uint8_t kReadStatusTx0If   = 0x02;
    static constexpr uint8_t kReadStatusTx0Req  = 0x04;
    static constexpr uint8_t kReadStatusTx1If   = 0x08;
    static constexpr uint8_t kReadStatusTx1Req  = 0x10;
    static constexpr uint8_t kReadStatusTx2If   = 0x20;
    static constexpr uint8_t kReadStatusTx2Req  = 0x40;
    static constexpr uint8_t kReadStatusCanIntf = 0x80;
private:

    // Initialization sub-steps (Part 4)
    bool init_gpio();
    bool init_spi();
    bool init_mcp2515_regs(bool cold_boot = true);

    Config m_cfg;
    std::atomic<bool> m_initialized{false};
    std::atomic<Mode> m_mode{Mode::Configuration};

    // ── ISR-driven RX state ───────────────────────────────────────
    // Cached second-buffer frame: when both RXB0 and RXB1 have data
    // in one notification cycle, the first is returned immediately
    // and the second is cached for zero-latency access on next call.
    can::Frame m_pending_frame{};
    bool       m_has_pending = false;

    // ── Overflow telemetry ────────────────────────────────────────
    std::atomic<uint16_t> m_rx_overflow_count{0};

    // ── SPI transport health (diagnostics, 1 Hz read by t_can_tx_high)
    std::atomic<uint32_t> m_spi_fail_count{0};

    // ── Bus-off detection (set by ISR via receive path) ───────────
    std::atomic<bool> m_bus_off{false};
    std::atomic<bool> m_recovering{false};
    std::atomic<uint32_t> m_recovery_attempts{0};
    std::atomic<uint32_t> m_recovery_failures{0};
    std::atomic<int64_t> m_bus_off_started_us{0};
    std::atomic<bool> m_first_rx_pending{false};
    std::atomic<bool> m_first_tx_pending{false};
};

}  // namespace rt
