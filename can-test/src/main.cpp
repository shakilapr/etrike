// CAN smoke + auto software seeker for ESP32-S3 N16R8.
//
// Roles:
//   BOARD_ROLE=0 (RT)  TX id 0x100 payload "RT"+seq
//   BOARD_ROLE=1 (SYS) TX id 0x200 payload "SY"+seq
//
// If TWAI_AUTO_SEEK=1 (default for role_rt_seek):
//   cycle pin maps (5/4 and 4/5) × modes (NORMAL, NO_ACK) every ~3s
//   until TX succeeds; stick with winning config.
//
// Watch CANalyst low (CH1) for 0x100 / 0x200.

#include <cstdio>
#include <cstring>
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef BOARD_ROLE
#define BOARD_ROLE 0
#endif
#ifndef TWAI_SWAP_TX_RX
#define TWAI_SWAP_TX_RX 0
#endif
#ifndef TWAI_FORCE_NO_ACK
#define TWAI_FORCE_NO_ACK 0
#endif
#ifndef TWAI_AUTO_SEEK
#define TWAI_AUTO_SEEK 0
#endif

constexpr int kBitrate = 500'000;
constexpr uint32_t kTxId = (BOARD_ROLE == 0) ? 0x100u : 0x200u;
constexpr const char* kRole = (BOARD_ROLE == 0) ? "RT" : "SYS";
constexpr const char* kTag = "can-smoke";

static gpio_num_t g_tx = GPIO_NUM_5;
static gpio_num_t g_rx = GPIO_NUM_4;
static twai_mode_t g_mode = TWAI_MODE_NORMAL;
static bool g_locked = false;

static twai_timing_config_t timing_500k() {
    twai_timing_config_t t{};
    t.quanta_resolution_hz = 8'000'000;
    t.tseg_1 = 11;
    t.tseg_2 = 4;
    t.sjw = 2;
    return t;
}

static const char* mode_name(twai_mode_t m) {
    switch (m) {
        case TWAI_MODE_NORMAL: return "NORMAL";
        case TWAI_MODE_NO_ACK: return "NO_ACK";
        case TWAI_MODE_LISTEN_ONLY: return "LISTEN";
        default: return "?";
    }
}

static bool twai_reinstall(gpio_num_t tx, gpio_num_t rx, twai_mode_t mode) {
    twai_stop();
    twai_driver_uninstall();
    g_tx = tx;
    g_rx = rx;
    g_mode = mode;

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT_V2(0, tx, rx, mode);
    g.tx_queue_len = 16;
    g.rx_queue_len = 32;
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_timing_config_t t = timing_500k();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
        ESP_LOGE(kTag, "install fail TX=%d RX=%d mode=%s", (int)tx, (int)rx, mode_name(mode));
        return false;
    }
    if (twai_start() != ESP_OK) {
        ESP_LOGE(kTag, "start fail");
        twai_driver_uninstall();
        return false;
    }
    ESP_LOGI(kTag, "TWAI up TX=GPIO%d RX=GPIO%d mode=%s", (int)tx, (int)rx, mode_name(mode));
    return true;
}

static bool tx_one(uint32_t seq) {
    twai_message_t tx{};
    tx.identifier = kTxId;
    tx.data_length_code = 4;
    tx.data[0] = static_cast<uint8_t>(kRole[0]);
    tx.data[1] = static_cast<uint8_t>(kRole[1]);
    tx.data[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    tx.data[3] = static_cast<uint8_t>(seq & 0xFF);
    return twai_transmit(&tx, pdMS_TO_TICKS(50)) == ESP_OK;
}

extern "C" void app_main() {
    // Initial pin preference from build flags
#if TWAI_SWAP_TX_RX
    gpio_num_t init_tx = GPIO_NUM_4, init_rx = GPIO_NUM_5;
#else
    gpio_num_t init_tx = GPIO_NUM_5, init_rx = GPIO_NUM_4;
#endif
#if TWAI_FORCE_NO_ACK
    twai_mode_t init_mode = TWAI_MODE_NO_ACK;
#else
    twai_mode_t init_mode = TWAI_MODE_NORMAL;
#endif

    ESP_LOGI(kTag, "=== CAN SMOKE role=%s id=0x%03lX auto_seek=%d force_noack=%d swap=%d ===",
             kRole, (unsigned long)kTxId, TWAI_AUTO_SEEK, TWAI_FORCE_NO_ACK, TWAI_SWAP_TX_RX);

    // Phase0 quick chip self-test
    if (!twai_reinstall(init_tx, init_rx, TWAI_MODE_NO_ACK)) {
        ESP_LOGE(kTag, "self-test install failed");
        return;
    }
    if (tx_one(0)) {
        ESP_LOGI(kTag, "self-test TX OK (NO_ACK)");
    } else {
        ESP_LOGW(kTag, "self-test TX FAIL even in NO_ACK — GPIO/controller issue?");
    }

    if (!twai_reinstall(init_tx, init_rx, init_mode)) {
        ESP_LOGE(kTag, "initial mode install failed");
        return;
    }

    // Seek configs for RT (or any board with AUTO_SEEK)
    struct Cfg { gpio_num_t tx, rx; twai_mode_t mode; };
    Cfg cfgs[] = {
        {GPIO_NUM_5, GPIO_NUM_4, TWAI_MODE_NORMAL},
        {GPIO_NUM_4, GPIO_NUM_5, TWAI_MODE_NORMAL},
        {GPIO_NUM_5, GPIO_NUM_4, TWAI_MODE_NO_ACK},
        {GPIO_NUM_4, GPIO_NUM_5, TWAI_MODE_NO_ACK},
    };
    int cfg_i = 0;
    // Align seek start with init
    for (int i = 0; i < 4; ++i) {
        if (cfgs[i].tx == init_tx && cfgs[i].rx == init_rx && cfgs[i].mode == init_mode) {
            cfg_i = i;
            break;
        }
    }

    uint32_t seq = 0, tx_ok = 0, tx_fail = 0, rx_n = 0;
    uint32_t consec_ok = 0, consec_fail = 0;
    int64_t next_tx = esp_timer_get_time() + 200'000;
    int64_t next_stat = esp_timer_get_time() + 1'000'000;
    int64_t next_seek = esp_timer_get_time() + 3'000'000;
    int64_t last_recovery = 0;

    while (true) {
        twai_message_t rx{};
        if (twai_receive(&rx, 0) == ESP_OK) {
            rx_n++;
            ESP_LOGI(kTag, "RX id=0x%03lX dlc=%u %02X %02X %02X %02X",
                     (unsigned long)rx.identifier, rx.data_length_code,
                     rx.data[0], rx.data[1], rx.data[2], rx.data[3]);
        }

        const int64_t now = esp_timer_get_time();

#if TWAI_AUTO_SEEK
        if (!g_locked && now >= next_seek) {
            cfg_i = (cfg_i + 1) % 4;
            Cfg c = cfgs[cfg_i];
            ESP_LOGW(kTag, "SEEK try TX=%d RX=%d mode=%s",
                     (int)c.tx, (int)c.rx, mode_name(c.mode));
            twai_reinstall(c.tx, c.rx, c.mode);
            consec_ok = consec_fail = 0;
            next_seek = now + 3'000'000;
        }
#endif

        if (now >= next_tx) {
            if (tx_one(seq++)) {
                tx_ok++;
                consec_ok++;
                consec_fail = 0;
#if TWAI_AUTO_SEEK
                // Lock after a few consecutive successes
                if (!g_locked && consec_ok >= 8) {
                    g_locked = true;
                    ESP_LOGI(kTag, "LOCKED config TX=GPIO%d RX=GPIO%d mode=%s",
                             (int)g_tx, (int)g_rx, mode_name(g_mode));
                }
#endif
            } else {
                tx_fail++;
                consec_fail++;
                consec_ok = 0;
                twai_status_info_t info{};
                twai_get_status_info(&info);
                if (tx_fail <= 5 || (tx_fail % 25) == 0) {
                    ESP_LOGW(kTag, "TX fail n=%lu state=%d tec=%lu rec=%lu cfg TX=%d RX=%d %s",
                             (unsigned long)tx_fail, (int)info.state,
                             (unsigned long)info.tx_error_counter,
                             (unsigned long)info.rx_error_counter,
                             (int)g_tx, (int)g_rx, mode_name(g_mode));
                }
                if ((info.state == TWAI_STATE_BUS_OFF || info.tx_error_counter >= 128)
                    && (now - last_recovery) > 1'000'000) {
                    last_recovery = now;
                    ESP_LOGW(kTag, "recover reinstall same cfg");
                    twai_reinstall(g_tx, g_rx, g_mode);
                }
            }
            next_tx = now + 200'000;
        }

        if (now >= next_stat) {
            twai_status_info_t info{};
            twai_get_status_info(&info);
            ESP_LOGI(kTag,
                     "STAT role=%s id=0x%03lX tx_ok=%lu fail=%lu rx=%lu tec=%lu rec=%lu "
                     "state=%d TX=%d RX=%d mode=%s locked=%d",
                     kRole, (unsigned long)kTxId,
                     (unsigned long)tx_ok, (unsigned long)tx_fail, (unsigned long)rx_n,
                     (unsigned long)info.tx_error_counter,
                     (unsigned long)info.rx_error_counter,
                     (int)info.state, (int)g_tx, (int)g_rx, mode_name(g_mode),
                     (int)g_locked);
            next_stat = now + 1'000'000;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
