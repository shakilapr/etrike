#pragma once
// MCP2515 CAN controller via SPI — high-level CAN bus.
// Architecture.md §7.2: SCK=36, MOSI=37, MISO=38, CS=39, INT=40.
// 10 MHz SPI, 500 kbit/s CAN.

#include <atomic>
#include <cstdint>
#include <cstddef>
#include "can/can_protocol.h"
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
    bool is_initialized() const { return m_initialized.load(std::memory_order_relaxed); }

    /// Switch operating mode. Returns false if the chip fails to enter
    /// the requested mode within 1ms. ListenOnly is safe for bus monitoring.
    bool set_mode(Mode mode);

    // ── Frame I/O (same API as can::CanDriver) ─────────────────────

    bool send(const can::Frame& frame, uint32_t timeout_ms = 2);
    bool receive(can::Frame& out, uint32_t timeout_ms = 100);

    // ── Diagnostics ────────────────────────────────────────────────

    void get_error_counters(uint8_t& tec, uint8_t& rec);
    bool bus_off() const { return m_bus_off.load(std::memory_order_relaxed); }
    void clear_bus_off() { m_bus_off.store(false, std::memory_order_relaxed); }

    // ── RX overflow telemetry ─────────────────────────────────────
    uint16_t rx_overflow_count() const { return m_rx_overflow_count.load(std::memory_order_relaxed); }
    void record_rx_overflow() { m_rx_overflow_count.fetch_add(1, std::memory_order_relaxed); }

private:
    // ── SPI primitives (ESP-IDF) ───────────────────────────────────
    void spi_write_byte(uint8_t addr, uint8_t data);
    uint8_t spi_read_byte(uint8_t addr);
    void spi_read_burst(uint8_t start_addr, uint8_t* data, size_t len);
    void spi_write_burst(uint8_t start_addr, const uint8_t* data, size_t len);
    void spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len);

    // ── MCP2515 register-level helpers ─────────────────────────────
    void reset();
    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);
    void modify_reg(uint8_t reg, uint8_t mask, uint8_t val);
    uint8_t read_status();
    void read_frame_burst(can::Frame& out, uint8_t base_addr);

    // ── MCP2515 register addresses ─────────────────────────────────
    static constexpr uint8_t kCmdReset      = 0xC0;
    static constexpr uint8_t kCmdRead       = 0x03;
    static constexpr uint8_t kCmdWrite      = 0x02;
    static constexpr uint8_t kCmdReadStatus = 0xA0;
    static constexpr uint8_t kCmdBitModify  = 0x05;
    static constexpr uint8_t kCmdRtsTx0     = 0x81;
    static constexpr uint8_t kCmdRtsTx2     = 0x84;

    // CNF registers for 500 kbit/s @ 8 MHz crystal
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
    static constexpr uint8_t kRegTxb2Data = 0x51;  // TXB2SIDH
    static constexpr uint8_t kRegRxb0Data  = 0x61;  // RXB0SIDH
    static constexpr uint8_t kRegRxb1Data  = 0x71;  // RXB1SIDH

    static constexpr uint8_t kReadStatusTx0Req = 0x01;
    static constexpr uint8_t kReadStatusTx2Req = 0x04;

    // Initialization sub-steps (Part 4)
    bool init_gpio();
    bool init_spi();
    bool init_mcp2515_regs();

    Config m_cfg;
    std::atomic<bool> m_initialized{false};

    // ── ISR-driven RX state ───────────────────────────────────────
    // Cached second-buffer frame: when both RXB0 and RXB1 have data
    // in one notification cycle, the first is returned immediately
    // and the second is cached for zero-latency access on next call.
    can::Frame m_pending_frame{};
    bool       m_has_pending = false;

    // ── Overflow telemetry ────────────────────────────────────────
    std::atomic<uint16_t> m_rx_overflow_count{0};

    // ── Bus-off detection (set by ISR via receive path) ───────────
    std::atomic<bool> m_bus_off{false};
};

}  // namespace rt
