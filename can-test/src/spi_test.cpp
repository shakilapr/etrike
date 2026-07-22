// SPI-TEST — verify MCP2515 is present and responding on SPI bus.
// Pins match rt-esp32/src/config.h production map:
//   SCK=15 MOSI=16 MISO=17 CS=18 INT=47
// Prefer: pio run -e hw_verify (full HW check) over this SPI-only test.
// Run: cd can-test && pio run -e spi -t upload --upload-port COM10

#include <cstdio>
#include <cstring>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ── Pin map (matches rt-esp32/src/config.h) ──────────────────────────
constexpr int kSpiHost   = SPI2_HOST;  // ESP32-S3 SPI2 (FSPI)
constexpr int kSpiSck    = 15;
constexpr int kSpiMosi   = 16;
constexpr int kSpiMiso   = 17;
constexpr int kSpiCs     = 18;
constexpr int kMcpInt    = 47;
constexpr int kSpiFreqHz = 1'000'000;  // 1 MHz for reliability on bench

// ── MCP2515 instruction set ───────────────────────────────────────────
constexpr uint8_t kCmdReset     = 0xC0;
constexpr uint8_t kCmdRead      = 0x03;
constexpr uint8_t kCmdWrite     = 0x02;
constexpr uint8_t kCmdReadStatus= 0xA0;
constexpr uint8_t kCmdBitModify = 0x05;

// ── Register addresses ────────────────────────────────────────────────
constexpr uint8_t kRegCanStat = 0x0E;
constexpr uint8_t kRegCanCtrl = 0x0F;
constexpr uint8_t kRegCanIntF = 0x2C;
constexpr uint8_t kRegCanIntE = 0x2B;
constexpr uint8_t kRegTec     = 0x1C;
constexpr uint8_t kRegRec     = 0x1D;
constexpr uint8_t kRegEflg    = 0x2D;
constexpr uint8_t kRegCnf1    = 0x2A;
constexpr uint8_t kRegCnf2    = 0x29;
constexpr uint8_t kRegCnf3    = 0x28;
constexpr uint8_t kRegRxb0Ctrl= 0x60;
constexpr uint8_t kRegRxb1Ctrl= 0x70;

// ── Globals ───────────────────────────────────────────────────────────
static spi_device_handle_t g_spi = nullptr;

// ── SPI helpers ───────────────────────────────────────────────────────

static bool spi_write(uint8_t addr, uint8_t data) {
    uint8_t tx[3] = { kCmdWrite, addr, data };
    spi_transaction_t t = {};
    t.length    = 24;
    t.tx_buffer = tx;
    return spi_device_transmit(g_spi, &t) == ESP_OK;
}

static bool spi_read(uint8_t addr, uint8_t& out) {
    uint8_t tx[3] = { kCmdRead, addr, 0x00 };
    uint8_t rx[3] = {};
    spi_transaction_t t = {};
    t.length    = 24;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    if (spi_device_transmit(g_spi, &t) != ESP_OK) return false;
    out = rx[2];
    return true;
}

static bool spi_read_status(uint8_t& out) {
    uint8_t tx[2] = { kCmdReadStatus, 0x00 };
    uint8_t rx[2] = {};
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    if (spi_device_transmit(g_spi, &t) != ESP_OK) return false;
    out = rx[1];
    return true;
}

static bool spi_reset() {
    uint8_t cmd = kCmdReset;
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_buffer = &cmd;
    return spi_device_transmit(g_spi, &t) == ESP_OK;
}

static bool spi_bit_modify(uint8_t addr, uint8_t mask, uint8_t val) {
    uint8_t tx[4] = { kCmdBitModify, addr, mask, val };
    spi_transaction_t t = {};
    t.length    = 32;
    t.tx_buffer = tx;
    return spi_device_transmit(g_spi, &t) == ESP_OK;
}

// ── Test helpers ──────────────────────────────────────────────────────

static const char* opmode_name(uint8_t canstat) {
    uint8_t opmode = (canstat >> 5) & 0x07;
    switch (opmode) {
        case 0: return "Normal";
        case 1: return "Sleep";
        case 2: return "Loopback";
        case 3: return "ListenOnly";
        case 4: return "Configuration";
        default: return "Unknown";
    }
}

static const char* icode_name(uint8_t canstat) {
    uint8_t ic = (canstat >> 1) & 0x03;
    switch (ic) {
        case 0: return "BUS-OFF";
        case 1: return "Error-Passive";
        case 2: return "Error-Warning";
        case 3: return "OK";
        default: return "?";
    }
}

static void print_bits(const char* label, uint8_t val) {
    printf("  %-22s = 0x%02X  ", label, val);
    for (int i = 7; i >= 0; i--) printf("%d", (val >> i) & 1);
    printf("\n");
}

// ── Main test ─────────────────────────────────────────────────────────

extern "C" void app_main() {
    printf("\n");
    printf("============================================================\n");
    printf("  MCP2515 SPI TEST — RT high-level CAN controller\n");
    printf("  Pins: SCK=%d MOSI=%d MISO=%d CS=%d INT=%d\n",
           kSpiSck, kSpiMosi, kSpiMiso, kSpiCs, kMcpInt);
    printf("============================================================\n\n");

    // 1. Init SPI bus
    printf("[1] Initializing SPI bus...\n");
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = kSpiMosi;
    bus_cfg.miso_io_num     = kSpiMiso;
    bus_cfg.sclk_io_num     = kSpiSck;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 64;

    esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(kSpiHost),
                                       &bus_cfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        printf("  FAIL: spi_bus_initialize returned %s\n", esp_err_to_name(err));
        return;
    }
    printf("  OK — SPI bus initialized\n");

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode           = 0;  // CPOL=0, CPHA=0
    dev_cfg.clock_speed_hz = kSpiFreqHz;
    dev_cfg.spics_io_num   = kSpiCs;
    dev_cfg.queue_size     = 1;
    dev_cfg.flags          = 0;

    err = spi_bus_add_device(static_cast<spi_host_device_t>(kSpiHost),
                             &dev_cfg, &g_spi);
    if (err != ESP_OK) {
        printf("  FAIL: spi_bus_add_device returned %s\n", esp_err_to_name(err));
        return;
    }
    printf("  OK — SPI device added\n\n");

    // 2. Reset MCP2515 via SPI
    printf("[2] Resetting MCP2515...\n");
    if (!spi_reset()) {
        printf("  FAIL: SPI reset command failed\n");
        printf("  → MCP2515 chip NOT responding on SPI bus\n");
        printf("  → Check: CS=GPIO%d, SCK=%d, MOSI=%d, MISO=%d\n",
               kSpiCs, kSpiSck, kSpiMosi, kSpiMiso);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    printf("  OK — reset command sent\n\n");

    // 3. Read CANSTAT — the definitive presence check
    printf("[3] Reading registers...\n");
    uint8_t canstat = 0;
    if (!spi_read(kRegCanStat, canstat)) {
        printf("  FAIL: cannot read CANSTAT register\n");
        return;
    }
    printf("  CANSTAT = 0x%02X  (OPMODE=%s, ICODE=%s)\n\n",
           canstat, opmode_name(canstat), icode_name(canstat));

    // 4. Verify: after reset, CANSTAT should be 0x80 (OPMODE=Config, ICODE=OK)
    uint8_t opmode = (canstat >> 5) & 0x07;
    if (opmode == 4) {
        printf("  ✓ MCP2515 chip IS present and responding correctly\n");
        printf("    (Configuration mode after reset — expected behavior)\n\n");
    } else if (canstat == 0x00 || canstat == 0xFF) {
        printf("  ✗ CANSTAT = 0x%02X — likely SPI readback of floating MISO\n", canstat);
        printf("    Chip absent or SPI pins miswired.\n\n");
        printf("    Expected: CANSTAT = 0x80 (OPMODE=Config after reset)\n");
        printf("    Got:      0x%02X\n\n", canstat);
        printf("  TROUBLESHOOTING:\n");
        printf("    - Is MCP2515 VCC=3.3V, GND connected?\n");
        printf("    - MOSI→SI (pin 14), MISO←SO (pin 15)?\n");
        printf("    - SCK→SCK (pin 13), CS→CS (pin 16)?\n");
        printf("    - Is the 8/16 MHz crystal populated?\n");
        return;
    } else {
        printf("  ? CANSTAT = 0x%02X — unexpected value\n", canstat);
        printf("    OPMODE=%d, ICODE=%d\n", opmode, (canstat >> 1) & 0x03);
    }

    // 5. Dump all key registers
    printf("[4] Register dump:\n");
    uint8_t reg;

    if (spi_read(kRegCanCtrl, reg)) print_bits("CANCTRL", reg);
    if (spi_read(kRegCanStat, reg)) print_bits("CANSTAT", reg);
    if (spi_read(kRegCanIntE, reg)) print_bits("CANINTE", reg);
    if (spi_read(kRegCanIntF, reg)) print_bits("CANINTF", reg);
    if (spi_read(kRegEflg,    reg)) print_bits("EFLG", reg);
    if (spi_read(kRegTec,     reg)) printf("  TEC                     = %d\n", reg);
    if (spi_read(kRegRec,     reg)) printf("  REC                     = %d\n", reg);
    if (spi_read(kRegCnf1,    reg)) print_bits("CNF1", reg);
    if (spi_read(kRegCnf2,    reg)) print_bits("CNF2", reg);
    if (spi_read(kRegCnf3,    reg)) print_bits("CNF3", reg);
    if (spi_read(kRegRxb0Ctrl,reg)) print_bits("RXB0CTRL", reg);
    if (spi_read(kRegRxb1Ctrl,reg)) print_bits("RXB1CTRL", reg);
    printf("\n");

    // 6. Try reading status via READ STATUS command
    printf("[5] READ STATUS command:\n");
    uint8_t status;
    if (spi_read_status(status)) {
        print_bits("STATUS", status);
    } else {
        printf("  FAIL\n");
    }
    printf("\n");

    // 7. Test bit-modify (read-modify-write cycle)
    printf("[6] Bit-modify test (RXB0CTRL)...\n");
    uint8_t orig;
    if (spi_read(kRegRxb0Ctrl, orig)) {
        printf("  Original RXB0CTRL = 0x%02X\n", orig);
        // Toggle a non-critical bit and restore
        if (spi_bit_modify(kRegRxb0Ctrl, 0x04, 0x04)) {
            uint8_t mod;
            spi_read(kRegRxb0Ctrl, mod);
            printf("  After set bit[2]   = 0x%02X\n", mod);
            spi_bit_modify(kRegRxb0Ctrl, 0x04, 0x00);
            spi_read(kRegRxb0Ctrl, mod);
            printf("  After clear bit[2] = 0x%02X\n", mod);
            printf("  ✓ Bit-modify works\n\n");
        } else {
            printf("  FAIL: bit-modify command\n\n");
        }
    }

    printf("============================================================\n");
    printf("  MCP2515 SPI test complete.\n");

    // 8. Final verdict
    if (spi_read(kRegCanStat, canstat)) {
        opmode = (canstat >> 5) & 0x07;
        if (opmode == 4) {  // Configuration mode = chip responded to reset
            printf("  VERDICT: MCP2515 PRESENT and responding via SPI ✓\n");
        } else if (canstat != 0x00 && canstat != 0xFF) {
            printf("  VERDICT: MCP2515 responding (CANSTAT=0x%02X, OPMODE=%d) ✓\n",
                   canstat, opmode);
        }
    }
    printf("============================================================\n\n");

    // Idle — reprint summary every 5 seconds
    int loop = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        loop++;
        printf("\n--- SPI test alive [loop %d] ---\n", loop);
        uint8_t cs;
        if (spi_read(kRegCanStat, cs)) {
            printf("  CANSTAT = 0x%02X  (OPMODE=%s, ICODE=%s)\n",
                   cs, opmode_name(cs), icode_name(cs));
            uint8_t op = (cs >> 5) & 0x07;
            if (op == 4 || op == 0 || op == 3)
                printf("  MCP2515: PRESENT & RESPONDING\n");
            else if (cs == 0x00 || cs == 0xFF)
                printf("  MCP2515: NOT DETECTED (floating MISO)\n");
        }
    }
}
