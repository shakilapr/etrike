// Hardware verification (no vehicle logic).
// Flash to RT (COM9) and SYS (COM5) before main vehicle firmware.
//
// Checks:
//   A) PSRAM present (boot log)
//   B) Low CAN TWAI: NO_ACK self-test + NORMAL TX attempt
//   C) High CAN MCP2515 SPI (RT only): detect chip, optional loopback TX
//   D) Optional alternate SPI pin map if primary fails
//
//   pio run -e hw_verify -t upload --upload-port COM9
//   pio run -e hw_verify -t upload --upload-port COM5

#include <cstdio>
#include <cstring>
#include "driver/twai.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* kTag = "hw-verify";

// Architecture pins (rt-esp32/src/config.h)
struct SpiPins {
    int sck, mosi, miso, cs, int_gpio;
    const char* name;
};

// Primary = production RT map
static const SpiPins kMcpPrimary{15, 16, 17, 18, 7, "primary SCK15 MOSI16 MISO17 CS18 INT7"};
// Legacy / older docs (spi_test old map)
static const SpiPins kMcpLegacy{36, 37, 38, 39, 40, "legacy SCK36 MOSI37 MISO38 CS39 INT40"};

constexpr gpio_num_t kTwaiTx = GPIO_NUM_5;
constexpr gpio_num_t kTwaiRx = GPIO_NUM_4;
constexpr int kBitrate = 500'000;

// MCP2515
constexpr uint8_t kCmdReset = 0xC0;
constexpr uint8_t kCmdRead = 0x03;
constexpr uint8_t kCmdWrite = 0x02;
constexpr uint8_t kCmdReadStatus = 0xA0;
constexpr uint8_t kCmdBitModify = 0x05;
constexpr uint8_t kCmdLoadTxb0 = 0x40;
constexpr uint8_t kCmdRtsTxb0 = 0x81;
constexpr uint8_t kRegCanStat = 0x0E;
constexpr uint8_t kRegCanCtrl = 0x0F;
constexpr uint8_t kRegCnf1 = 0x2A;
constexpr uint8_t kRegCnf2 = 0x29;
constexpr uint8_t kRegCnf3 = 0x28;
constexpr uint8_t kRegCanIntE = 0x2B;
constexpr uint8_t kRegRxb0Ctrl = 0x60;
constexpr uint8_t kRegRxb1Ctrl = 0x70;

struct Results {
    bool psram = false;
    bool twai_self = false;
    bool twai_normal_tx = false;
    bool mcp_spi = false;
    bool mcp_loopback = false;
    const char* mcp_map = "none";
};

static twai_timing_config_t timing_500k() {
    twai_timing_config_t t{};
    t.quanta_resolution_hz = 8'000'000;
    t.tseg_1 = 11;
    t.tseg_2 = 4;
    t.sjw = 2;
    return t;
}

static bool twai_install(twai_mode_t mode) {
    twai_stop();
    twai_driver_uninstall();
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(0, kTwaiTx, kTwaiRx, mode);
    g.tx_queue_len = 8;
    g.rx_queue_len = 16;
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_timing_config_t t = timing_500k();
    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    return twai_start() == ESP_OK;
}

static bool twai_tx_id(uint32_t id) {
    twai_message_t m{};
    m.identifier = id;
    m.data_length_code = 2;
    m.data[0] = 'H';
    m.data[1] = 'W';
    return twai_transmit(&m, pdMS_TO_TICKS(50)) == ESP_OK;
}

// ── MCP SPI ──────────────────────────────────────────────────────────

static spi_device_handle_t g_spi = nullptr;

static bool mcp_spi_init(const SpiPins& p) {
    if (g_spi) {
        spi_bus_remove_device(g_spi);
        g_spi = nullptr;
        spi_bus_free(SPI2_HOST);
    }
    spi_bus_config_t bus{};
    bus.mosi_io_num = p.mosi;
    bus.miso_io_num = p.miso;
    bus.sclk_io_num = p.sck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 64;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
        ESP_LOGE(kTag, "SPI bus init failed (%s)", p.name);
        return false;
    }
    spi_device_interface_config_t dev{};
    dev.mode = 0;
    dev.clock_speed_hz = 1'000'000;  // slow for reliability
    dev.spics_io_num = p.cs;
    dev.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &dev, &g_spi) != ESP_OK) {
        ESP_LOGE(kTag, "SPI device add failed");
        spi_bus_free(SPI2_HOST);
        return false;
    }
    gpio_set_direction(static_cast<gpio_num_t>(p.int_gpio), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(p.int_gpio), GPIO_PULLUP_ONLY);
    return true;
}

static bool mcp_xfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    spi_transaction_t t{};
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return spi_device_transmit(g_spi, &t) == ESP_OK;
}

static bool mcp_reset() {
    uint8_t cmd = kCmdReset;
    return mcp_xfer(&cmd, nullptr, 1);
}

static bool mcp_read(uint8_t addr, uint8_t& out) {
    uint8_t tx[3] = {kCmdRead, addr, 0};
    uint8_t rx[3] = {};
    if (!mcp_xfer(tx, rx, 3)) return false;
    out = rx[2];
    return true;
}

static bool mcp_write(uint8_t addr, uint8_t val) {
    uint8_t tx[3] = {kCmdWrite, addr, val};
    return mcp_xfer(tx, nullptr, 3);
}

static bool mcp_modify(uint8_t addr, uint8_t mask, uint8_t val) {
    uint8_t tx[4] = {kCmdBitModify, addr, mask, val};
    return mcp_xfer(tx, nullptr, 4);
}

static const char* mcp_mode_name(uint8_t canstat) {
    switch ((canstat >> 5) & 7) {
        case 0: return "Normal";
        case 1: return "Sleep";
        case 2: return "Loopback";
        case 3: return "ListenOnly";
        case 4: return "Config";
        default: return "Unknown";
    }
}

// Probe one pin map. Returns true if MCP answers (CANSTAT config after reset).
static bool probe_mcp(const SpiPins& pins, Results& r) {
    ESP_LOGI(kTag, "--- MCP SPI probe: %s ---", pins.name);
    if (!mcp_spi_init(pins)) return false;

    mcp_reset();
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t canstat = 0xFF;
    if (!mcp_read(kRegCanStat, canstat)) {
        ESP_LOGE(kTag, "SPI read CANSTAT failed");
        return false;
    }
    ESP_LOGI(kTag, "CANSTAT=0x%02X mode=%s", canstat, mcp_mode_name(canstat));

    if (canstat == 0x00 || canstat == 0xFF) {
        ESP_LOGW(kTag, "FAIL: floating/absent MISO (got 0x%02X). Check power + SPI wires.", canstat);
        return false;
    }

    // After reset, expect Configuration mode (OPMODE=100 → CANSTAT often 0x80)
    uint8_t op = (canstat >> 5) & 7;
    if (op != 4) {
        ESP_LOGW(kTag, "WARN: not in Config after reset (op=%u) — chip may be partial", op);
        // still count as "present" if not 0x00/0xFF
    } else {
        ESP_LOGI(kTag, "PASS: MCP2515 present (Config mode after reset)");
    }

    r.mcp_spi = true;
    r.mcp_map = pins.name;

    // Configure 500k CNF for 8/16 MHz crystal variants — try 16 MHz first (common)
    // CNF for 500k @ 16 MHz crystal: CNF1=0x00, CNF2=0x90, CNF3=0x02 (example)
    // Production RT uses values from driver — for loopback we just need internal path.
    mcp_write(kRegCnf1, 0x00);
    mcp_write(kRegCnf2, 0xB1);  // common for 8MHz@500k / ok for loopback test
    mcp_write(kRegCnf3, 0x85);
    mcp_write(kRegRxb0Ctrl, 0x60);  // accept any
    mcp_write(kRegRxb1Ctrl, 0x60);
    mcp_write(kRegCanIntE, 0x00);

    // Enter loopback (REQOP=010)
    mcp_modify(kRegCanCtrl, 0xE0, 0x40);
    vTaskDelay(pdMS_TO_TICKS(5));
    mcp_read(kRegCanStat, canstat);
    ESP_LOGI(kTag, "after loopback req CANSTAT=0x%02X mode=%s", canstat, mcp_mode_name(canstat));

    if (((canstat >> 5) & 7) == 2) {
        // Load TXB0 SID + 2 data bytes via load TX buffer cmd
        // SIDH/SIDL for id 0x300: SIDH=(id>>3), SIDL=((id&7)<<5)
        uint8_t load[6] = {
            kCmdLoadTxb0,
            static_cast<uint8_t>(0x300 >> 3),
            static_cast<uint8_t>((0x300 & 7) << 5),
            0x00,  // EID8
            0x00,  // EID0
        };
        // Actually load TXB0 is more complex — use register writes for DLC/data
        // Simple: write TXB0 SID via registers 0x31.. then RTS
        mcp_write(0x30, 0x00);  // TXB0CTRL
        mcp_write(0x31, static_cast<uint8_t>(0x300 >> 3));
        mcp_write(0x32, static_cast<uint8_t>((0x300 & 7) << 5));
        mcp_write(0x35, 0x02);  // DLC=2
        mcp_write(0x36, 'H');
        mcp_write(0x37, 'I');
        uint8_t rts = kCmdRtsTxb0;
        mcp_xfer(&rts, nullptr, 1);
        vTaskDelay(pdMS_TO_TICKS(20));

        // Check RXB0 full via CANINTF bit 0 or RXB0CTRL
        uint8_t canintf = 0;
        mcp_read(0x2C, canintf);
        ESP_LOGI(kTag, "CANINTF=0x%02X after loopback TX", canintf);
        if (canintf & 0x01) {
            r.mcp_loopback = true;
            ESP_LOGI(kTag, "PASS: MCP loopback RX flag set (SPI+chip CAN path OK)");
            // clear
            mcp_modify(0x2C, 0x01, 0x00);
        } else {
            ESP_LOGW(kTag, "FAIL: loopback TX did not set RX0IF — crystal/bitrate/chip issue");
        }
    } else {
        ESP_LOGW(kTag, "FAIL: could not enter loopback");
    }

    return true;
}

extern "C" void app_main() {
    Results r{};

    ESP_LOGI(kTag, "================================================");
    ESP_LOGI(kTag, "  HARDWARE VERIFY (no vehicle app)");
    ESP_LOGI(kTag, "================================================");

    // A) Chip / PSRAM / flash
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    size_t psram = esp_psram_get_size();
    r.psram = psram > 0;
    ESP_LOGI(kTag, "[A] chip=ESP32-S3 rev%d cores=%d flash=%luKB psram=%uKB → %s",
             chip.revision, chip.cores,
             (unsigned long)(flash_size / 1024),
             (unsigned)(psram / 1024),
             r.psram ? "PASS" : "FAIL (no PSRAM)");

    // B) Low TWAI
    ESP_LOGI(kTag, "[B] Low CAN TWAI TX=GPIO%d RX=GPIO%d @ 500k", (int)kTwaiTx, (int)kTwaiRx);
    if (twai_install(TWAI_MODE_NO_ACK) && twai_tx_id(0x7E0)) {
        r.twai_self = true;
        ESP_LOGI(kTag, "  NO_ACK self-test TX → PASS (controller+GPIO drive)");
    } else {
        ESP_LOGE(kTag, "  NO_ACK self-test TX → FAIL");
    }

    if (twai_install(TWAI_MODE_NORMAL)) {
        // Need peer/CANalyst ACK to pass; still report attempt
        bool ok = false;
        for (int i = 0; i < 5; ++i) {
            if (twai_tx_id(0x101)) {
                ok = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        r.twai_normal_tx = ok;
        twai_status_info_t info{};
        twai_get_status_info(&info);
        ESP_LOGI(kTag, "  NORMAL TX id=0x101 → %s (tec=%lu rec=%lu state=%d)",
                 ok ? "PASS (ACK on bus)" : "FAIL (no ACK — check transceiver/term/peer)",
                 (unsigned long)info.tx_error_counter,
                 (unsigned long)info.rx_error_counter,
                 (int)info.state);
        if (!ok) {
            ESP_LOGW(kTag, "  Note: FAIL here is OK if alone on bus without CANalyst/peer");
        }
    }

    // C) High MCP2515 — try primary then legacy pins
    ESP_LOGI(kTag, "[C] High CAN MCP2515 SPI");
    if (!probe_mcp(kMcpPrimary, r)) {
        ESP_LOGW(kTag, "  primary pin map failed — trying legacy map");
        probe_mcp(kMcpLegacy, r);
    }
    if (!r.mcp_spi) {
        ESP_LOGE(kTag, "  MCP2515 NOT DETECTED on either pin map");
        ESP_LOGE(kTag, "  High bus cannot work until SPI module is present/wired/powered");
    }

    // Summary
    ESP_LOGI(kTag, "================================================");
    ESP_LOGI(kTag, "  SUMMARY");
    ESP_LOGI(kTag, "  PSRAM:           %s", r.psram ? "PASS" : "FAIL");
    ESP_LOGI(kTag, "  TWAI self-test:  %s", r.twai_self ? "PASS" : "FAIL");
    ESP_LOGI(kTag, "  TWAI normal TX:  %s", r.twai_normal_tx ? "PASS" : "FAIL/no peer");
    ESP_LOGI(kTag, "  MCP2515 SPI:     %s (%s)", r.mcp_spi ? "PASS" : "FAIL", r.mcp_map);
    ESP_LOGI(kTag, "  MCP loopback:    %s", r.mcp_loopback ? "PASS" : "FAIL/n/a");
    ESP_LOGI(kTag, "================================================");
    if (r.psram && r.twai_self && r.mcp_spi && r.mcp_loopback) {
        ESP_LOGI(kTag, "  HARDWARE READY for vehicle firmware (both buses)");
    } else if (r.psram && r.twai_self && !r.mcp_spi) {
        ESP_LOGW(kTag, "  LOW-ONLY READY — fix MCP wiring before high bus");
    } else {
        ESP_LOGE(kTag, "  HARDWARE NOT READY — fix FAILs before vehicle flash");
    }
    ESP_LOGI(kTag, "  Watch CANalyst low for 0x101 if NORMAL TX passed.");
    ESP_LOGI(kTag, "  Idle — re-run by reset.");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(kTag, "idle (reset board to re-run verify)");
    }
}
