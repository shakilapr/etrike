/**
 * debug-esp32 — ESP32-S3 CAN-to-MQTT Bridge
 *
 * Reads CAN frames from TWAI (GPIO 5/4) and optional MCP2515 (SPI),
 * decodes them, and publishes JSON over MQTT to the debug backend.
 * Accepts injection commands via MQTT and writes them to the CAN bus.
 *
 * FreeRTOS tasks:
 *   can_rx_a (prio 5) — TWAI receive → decode queue
 *   can_rx_b (prio 5) — MCP2515 receive → decode queue (optional)
 *   can_decode (prio 4) — ID dispatch → JSON → MQTT publish
 *   mqtt_tx    (prio 3) — MQTT publish queue consumer
 *   cmd_rx     (prio 3) — MQTT subscribe → command queue
 *   can_inject (prio 3) — command queue → CAN TX
 *   stats      (prio 1) — 1 Hz stats aggregation
 *   status     (prio 2) — 5 s heartbeat
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "debug-esp32";

// ── Configuration ──
// Override via Kconfig (idf.py menuconfig) or define defaults here for CI.
#ifndef CONFIG_DEBUG_WIFI_SSID
#define WIFI_SSID      "debug-ap"
#else
#define WIFI_SSID      CONFIG_DEBUG_WIFI_SSID
#endif
#ifndef CONFIG_DEBUG_WIFI_PASSWORD
#define WIFI_PASS      "debug-pass"
#else
#define WIFI_PASS      CONFIG_DEBUG_WIFI_PASSWORD
#endif
#ifndef CONFIG_DEBUG_MQTT_BROKER
#define MQTT_BROKER    "mqtt://192.168.1.1"
#else
#define MQTT_BROKER    CONFIG_DEBUG_MQTT_BROKER
#endif
#define MQTT_PORT      1883

#define TWAI_TX_PIN    GPIO_NUM_5
#define TWAI_RX_PIN    GPIO_NUM_4

// ── CAN frame types ──
typedef struct {
    uint8_t  bus;       // 0 = high, 1 = low
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint64_t ts_us;
} can_frame_t;

// ── Queues ──
static QueueHandle_t decode_q;
static QueueHandle_t mqtt_tx_q;
static QueueHandle_t cmd_q;

// ── Stats ──
static struct {
    uint32_t rx_total[2];
    uint32_t rx_by_id[2][0x800];
    uint32_t tx_total[2];
    uint8_t  tec[2];
    uint8_t  rec[2];
    uint32_t last_report_s;
} g_stats;

// ── MQTT client ──
static esp_mqtt_client_handle_t mqtt_client;

// ── Forward declarations ──
static void task_can_rx_a(void *arg);
static void task_can_rx_b(void *arg);
static void task_can_decode(void *arg);
static void task_mqtt_tx(void *arg);
static void task_cmd_rx(void *arg);
static void task_can_inject(void *arg);
static void task_stats(void *arg);
static void task_status(void *arg);
static void wifi_init(void);
static void mqtt_init(void);
static char *frame_to_json(const can_frame_t *frame);
[[maybe_unused]] static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);

// ── Main ──
extern "C" void app_main(void) {
    ESP_LOGI(TAG, "debug-esp32 starting");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Create queues
    decode_q  = xQueueCreate(64, sizeof(can_frame_t));
    mqtt_tx_q = xQueueCreate(64, sizeof(char *));
    cmd_q     = xQueueCreate(16, sizeof(can_frame_t));

    // Init TWAI
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_PIN, TWAI_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config  = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();
    ESP_LOGI(TAG, "TWAI started on GPIO %d/%d", TWAI_TX_PIN, TWAI_RX_PIN);

    // Wi-Fi + MQTT
    wifi_init();
    mqtt_init();

    // Create tasks
    xTaskCreatePinnedToCore(task_can_rx_a,  "can_rx_a",  2048, NULL, 5, NULL, 0);
    // xTaskCreatePinnedToCore(task_can_rx_b,  "can_rx_b",  2048, NULL, 5, NULL, 0);  // uncomment when MCP2515 present
    xTaskCreatePinnedToCore(task_can_decode,"can_decode",3072, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(task_mqtt_tx,   "mqtt_tx",   2048, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_cmd_rx,    "cmd_rx",    2048, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_can_inject,"can_inject",2048, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_stats,     "stats",     1536, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(task_status,    "status",    1536, NULL, 2, NULL, 0);

    ESP_LOGI(TAG, "All tasks created");
}

// ── Wi-Fi ──
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi connected, got IP");
    }
}

static void wifi_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// ── MQTT ──
[[maybe_unused]] static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(mqtt_client, "etrike/debug/cmd/send", 1);
        esp_mqtt_client_subscribe(mqtt_client, "etrike/debug/cmd/send/periodic", 1);
        break;
    case MQTT_EVENT_DATA: {
        // Parse command and push to cmd_q
        char *payload = strndup(event->data, event->data_len);
        can_frame_t cmd = {0};
        // Minimal: parse JSON for id, bus, dlc, data
        cJSON *root = cJSON_Parse(payload);
        if (root) {
            cJSON *j_id = cJSON_GetObjectItem(root, "id");
            cJSON *j_bus = cJSON_GetObjectItem(root, "bus");
            cJSON *j_dlc = cJSON_GetObjectItem(root, "dlc");
            cJSON *j_data = cJSON_GetObjectItem(root, "data");
            if (j_id && j_id->valuestring) {
                cmd.id = (uint32_t)strtol(j_id->valuestring, NULL, 16);
            }
            if (j_bus && j_bus->valuestring) {
                cmd.bus = strcmp(j_bus->valuestring, "low") == 0 ? 1 : 0;
            }
            if (j_dlc) cmd.dlc = (uint8_t)j_dlc->valueint;
            if (j_data && cJSON_IsArray(j_data)) {
                for (int i = 0; i < cJSON_GetArraySize(j_data) && i < 8; i++) {
                    cmd.data[i] = (uint8_t)cJSON_GetArrayItem(j_data, i)->valueint;
                }
            }
            xQueueSend(cmd_q, &cmd, 0);
            cJSON_Delete(root);
        }
        free(payload);
        break;
    }
    default:
        break;
    }
}

static void mqtt_init(void) {
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = "mqtt://" MQTT_BROKER;
    mqtt_cfg.broker.address.port = MQTT_PORT;
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// ── CAN RX task (TWAI) ──
static void task_can_rx_a(void *arg) {
    twai_message_t msg;
    while (1) {
        if (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
            can_frame_t frame = {
                .bus   = 0,  // high
                .id    = msg.identifier,
                .dlc   = msg.data_length_code,
                .ts_us = (uint64_t)esp_timer_get_time(),
            };
            memcpy(frame.data, msg.data, msg.data_length_code);
            xQueueSend(decode_q, &frame, 0);
        }
    }
}

// ── CAN RX task (MCP2515) — stub ──
[[maybe_unused]] static void task_can_rx_b(void *arg) {
    // TODO: implement when MCP2515 hardware is present
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── Decode: ID dispatch → JSON → MQTT publish queue ──
static void task_can_decode(void *arg) {
    can_frame_t frame;
    while (1) {
        if (xQueueReceive(decode_q, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
            char *json = frame_to_json(&frame);
            if (json) {
                xQueueSend(mqtt_tx_q, &json, 0);
            }
        }
    }
}

// ── MQTT TX: publish queued JSON strings ──
static void task_mqtt_tx(void *arg) {
    char *json;
    while (1) {
        if (xQueueReceive(mqtt_tx_q, &json, pdMS_TO_TICKS(100)) == pdTRUE) {
            const char *bus = "high";  // TODO: read from frame
            char topic[64];
            snprintf(topic, sizeof(topic), "etrike/debug/can/rx/%s", bus);
            esp_mqtt_client_publish(mqtt_client, topic, json, 0, 1, 0);
            free(json);
        }
    }
}

// ── Command RX (stub — handled in MQTT event) ──
static void task_cmd_rx(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── CAN Inject: write queued commands to TWAI ──
static void task_can_inject(void *arg) {
    can_frame_t cmd;
    while (1) {
        if (xQueueReceive(cmd_q, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            twai_message_t msg = {
                .identifier       = cmd.id,
                .data_length_code = cmd.dlc,
            };
            memcpy(msg.data, cmd.data, cmd.dlc);
            twai_transmit(&msg, pdMS_TO_TICKS(100));
            g_stats.tx_total[cmd.bus]++;
        }
    }
}

// ── Stats: 1 Hz report ──
static void task_stats(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Read TEC/REC
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            g_stats.tec[0] = status.tx_error_counter;
            g_stats.rec[0] = status.rx_error_counter;
        }

        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "ts", esp_timer_get_time() / 1000000.0);
        cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);

        cJSON *buses = cJSON_CreateObject();
        for (int b = 0; b < 2; b++) {
            cJSON *bus = cJSON_CreateObject();
            cJSON_AddBoolToObject(bus, "active", g_stats.rx_total[b] > 0);
            cJSON_AddNumberToObject(bus, "total", g_stats.rx_total[b]);
            cJSON_AddNumberToObject(bus, "fps", 0);  // calculated by backend
            cJSON_AddNumberToObject(bus, "load_pct", 0);
            cJSON_AddNumberToObject(bus, "tec", g_stats.tec[b]);
            cJSON_AddNumberToObject(bus, "rec", g_stats.rec[b]);

            cJSON *by_id = cJSON_CreateObject();
            for (int id = 0; id < 0x800; id++) {
                if (g_stats.rx_by_id[b][id] > 0) {
                    char key[8];
                    snprintf(key, sizeof(key), "0x%03X", (unsigned int)id);
                    cJSON_AddNumberToObject(by_id, key, g_stats.rx_by_id[b][id]);
                }
            }
            cJSON_AddItemToObject(bus, "by_id", by_id);
            cJSON_AddItemToObject(buses, b == 0 ? "high" : "low", bus);
        }
        cJSON_AddItemToObject(root, "buses", buses);

        char *json = cJSON_PrintUnformatted(root);
        esp_mqtt_client_publish(mqtt_client, "etrike/debug/can/stats", json, 0, 1, 0);
        free(json);
        cJSON_Delete(root);
    }
}

// ── Status: 5 s heartbeat ──
static void task_status(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "online", true);
        cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);
        cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

        char *json = cJSON_PrintUnformatted(root);
        esp_mqtt_client_publish(mqtt_client, "etrike/debug/status", json, 0, 1, 0);
        esp_mqtt_client_publish(mqtt_client, "etrike/debug/uptime", json, 0, 1, 0);
        free(json);
        cJSON_Delete(root);
    }
}

// ── Frame → JSON ──
static char *frame_to_json(const can_frame_t *frame) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts", frame->ts_us / 1000000.0);
    cJSON_AddStringToObject(root, "bus", frame->bus == 1 ? "low" : "high");

    char id_str[8];
    snprintf(id_str, sizeof(id_str), "0x%03lX", frame->id);
    cJSON_AddStringToObject(root, "id", id_str);
    cJSON_AddNumberToObject(root, "dlc", frame->dlc);

    cJSON *data = cJSON_CreateArray();
    for (int i = 0; i < frame->dlc; i++) {
        cJSON_AddItemToArray(data, cJSON_CreateNumber(frame->data[i]));
    }
    cJSON_AddItemToObject(root, "data", data);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
