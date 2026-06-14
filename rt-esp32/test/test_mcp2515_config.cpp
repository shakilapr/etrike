// Phase R5: MCP2515 driver — config + register validation (host).
// Full hardware test requires ESP32 + MCP2515 module + logic analyzer.
// g++ -std=c++17 -I. -I../src -I../../shared test_mcp2515_config.cpp -o test_mcp2515_config

#include <cstdio>
#include "can_driver_mcp2515.h"
#include "can_driver_twai.h"
#include "config.h"

static int fails = 0;
#define CHECK(desc) printf("  %-55s ", desc)
#define OK          printf("PASS\n")
#define BAD(m)      do { printf("FAIL: %s\n", m); ++fails; } while(0)

static void hdr(const char* s) { printf("\n== %s ==\n", s); }

int main() {
    printf("Phase R5: RT dual-CAN driver (config validation)\n");

    // ── CanDriver API (shared, used for low bus) ───────────────────
    hdr("CanDriver (low bus — TWAI)");
    {
        can::CanDriver::Config cfg;
        cfg.tx_gpio    = rt::kCanLowTxGpio;
        cfg.rx_gpio    = rt::kCanLowRxGpio;
        cfg.bitrate_hz = rt::kCanLowBitrateHz;
        CHECK("TWAI config: TX=5 RX=4 500kbit");
        if (cfg.tx_gpio == 5 && cfg.rx_gpio == 4 && cfg.bitrate_hz == 500'000) OK;
        else BAD("twai config");
    }

    // ── Mcp2515Driver config ──────────────────────────────────────
    hdr("Mcp2515Driver config (high bus)");
    {
        auto cfg = rt::Mcp2515Driver::default_config();
        CHECK("SPI pins: SCK=36 MOSI=37 MISO=38 CS=39 INT=40");
        if (cfg.sck_gpio == 36 && cfg.mosi_gpio == 37 && cfg.miso_gpio == 38
            && cfg.cs_gpio == 39 && cfg.int_gpio == 40) OK; else BAD("spi pins");

        CHECK("SPI freq = 10 MHz");
        if (cfg.spi_freq == 10'000'000) OK; else BAD("spi freq");

        // Verify no pin conflicts between low CAN and high CAN
        CHECK("TWAI TX/RX (5,4) don't overlap MCP2515 (36-40)");
        bool no_conflict = (rt::kCanLowTxGpio != 36 && rt::kCanLowTxGpio != 37
                         && rt::kCanLowTxGpio != 38 && rt::kCanLowTxGpio != 39
                         && rt::kCanLowTxGpio != 40 && rt::kCanLowRxGpio != 36
                         && rt::kCanLowRxGpio != 37 && rt::kCanLowRxGpio != 38
                         && rt::kCanLowRxGpio != 39 && rt::kCanLowRxGpio != 40);
        if (no_conflict) OK; else BAD("pin conflict");
    }

    // ── TWAI functions exist ──────────────────────────────────────
    hdr("TWAI driver functions");
    {
        CHECK("can_low_init signature compiles");
        // Just test that the function pointer type is correct
        bool (*init_fn)(int, int, int) = rt::can_low_init;
        (void)init_fn;
        OK;
    }
    {
        CHECK("can_low_driver signature compiles");
        can::CanDriver* (*get_fn)() = rt::can_low_driver;
        (void)get_fn;
        OK;
    }

    // ── CAN frame round-trip through any driver ───────────────────
    hdr("CAN frame wire-format stability");
    {
        // Ensure the Frame struct is compatible with both TWAI and MCP2515
        can::Frame f;
        f.id = 0x202; f.dlc = 5;
        f.put_i32(0, 1500);
        f.put_u8(4, uint8_t(can::Gear::D));

        CHECK("Frame {0x202, DLC=5, speed=1500, gear=D}");
        if (f.id == 0x202 && f.dlc == 5
            && f.i32_at(0) == 1500 && f.u8_at(4) == uint8_t(can::Gear::D)) OK;
        else BAD("frame");
    }

    printf("\n  Result: %d failures\n\n", fails);
    return fails ? 1 : 0;
}
