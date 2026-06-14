#pragma once
// MCP2515 CAN controller via SPI — high-level CAN bus.
// Architecture.md §7.2: SCK=36, MOSI=37, MISO=38, CS=39, INT=40.
// 10 MHz SPI, 500 kbit/s CAN.

#include <cstdint>
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
                 rt::kSpiCsGpio, rt::kMcpIntGpio, 2, 10'000'000 };
    }

    Mcp2515Driver() : m_cfg(default_config()) {}
    explicit Mcp2515Driver(const Config& cfg) : m_cfg(cfg) {}

    Mcp2515Driver(const Mcp2515Driver&) = delete;
    Mcp2515Driver& operator=(const Mcp2515Driver&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────

    bool init();
    bool is_initialized() const { return m_initialized; }

    // ── Frame I/O (same API as can::CanDriver) ─────────────────────

    bool send(const can::Frame& frame, uint32_t timeout_ms = 10);
    bool receive(can::Frame& out, uint32_t timeout_ms = 100);

    // ── Diagnostics ────────────────────────────────────────────────

    void get_error_counters(uint8_t& tec, uint8_t& rec) const;

private:
    // ── SPI primitives (ESP-IDF) ───────────────────────────────────
    void spi_write_byte(uint8_t addr, uint8_t data);
    uint8_t spi_read_byte(uint8_t addr);
    void spi_write_buf(uint8_t addr, const uint8_t* data, size_t len);
    void spi_read_buf(uint8_t addr, uint8_t* data, size_t len);
    void spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len);

    // ── MCP2515 register-level helpers ─────────────────────────────
    void reset();
    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);
    void modify_reg(uint8_t reg, uint8_t mask, uint8_t val);
    uint8_t read_status();

    // ── MCP2515 register addresses ─────────────────────────────────
    static constexpr uint8_t kCmdReset      = 0xC0;
    static constexpr uint8_t kCmdRead       = 0x03;
    static constexpr uint8_t kCmdWrite      = 0x02;
    static constexpr uint8_t kCmdReadStatus = 0xA0;
    static constexpr uint8_t kCmdRxStatus   = 0xB0;
    static constexpr uint8_t kCmdBitModify  = 0x05;
    static constexpr uint8_t kCmdLoadTx0    = 0x40;
    static constexpr uint8_t kCmdRtsTx0     = 0x81;

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

    Config m_cfg;
    bool   m_initialized = false;
};

}  // namespace rt
