/**
 * @file board_waveshare_43b.c
 * @brief Board Support Package implementation for Waveshare ESP32-S3-Touch-LCD-4.3B-BOX
 */

#include "board_waveshare_43b.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_rgb.h"

static const char *TAG = "BOARD_43B";

#define CH422G_I2C_ADDR        0x24

/* Static TWAI Driver Configuration */
static const twai_general_config_t g_config =
    TWAI_GENERAL_CONFIG_DEFAULT(BOARD_CAN_TX_PIN, BOARD_CAN_RX_PIN, TWAI_MODE_NORMAL);
static const twai_timing_config_t t_config =
    TWAI_TIMING_CONFIG_500KBITS();
static const twai_filter_config_t f_config =
    TWAI_FILTER_CONFIG_ACCEPT_ALL();

esp_err_t board_twai_init(void) {
    ESP_LOGI(TAG, "Initializing TWAI on TX=GPIO%d, RX=GPIO%d @ %d kbps",
             BOARD_CAN_TX_PIN, BOARD_CAN_RX_PIN, BOARD_CAN_BAUDRATE_KBPS);

    ESP_RETURN_ON_ERROR(twai_driver_install(&g_config, &t_config, &f_config), TAG, "TWAI driver install failed");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "TWAI start failed");

    ESP_LOGI(TAG, "TWAI CAN driver active & running");
    return ESP_OK;
}

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_PIN,
        .scl_io_num = BOARD_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_PORT, &conf), TAG, "I2C config failed");
    return i2c_driver_install(BOARD_I2C_PORT, conf.mode, 0, 0, 0);
}

esp_err_t board_set_backlight(uint8_t percent) {
    // Controlled through CH422G I/O Expander command register
    uint8_t cmd_data[2] = {0x01, (uint8_t)((percent > 0) ? 0x01 : 0x00)};
    return i2c_master_write_to_device(BOARD_I2C_PORT, CH422G_I2C_ADDR, cmd_data, sizeof(cmd_data), pdMS_TO_TICKS(50));
}

esp_err_t board_read_isolated_inputs(bool *di0, bool *di1) {
    if (!di0 || !di1) return ESP_ERR_INVALID_ARG;
    uint8_t in_state = 0;
    esp_err_t err = i2c_master_read_from_device(BOARD_I2C_PORT, CH422G_I2C_ADDR, &in_state, 1, pdMS_TO_TICKS(50));
    if (err == ESP_OK) {
        *di0 = (in_state & 0x01) != 0;
        *di1 = (in_state & 0x20) != 0;
    }
    return err;
}

esp_err_t board_set_isolated_outputs(bool do0, bool do1) {
    uint8_t out_byte = 0x00;
    if (do0) out_byte |= 0x01;
    if (do1) out_byte |= 0x02;
    uint8_t cmd[2] = {0x02, out_byte};
    return i2c_master_write_to_device(BOARD_I2C_PORT, CH422G_I2C_ADDR, cmd, 2, pdMS_TO_TICKS(50));
}

esp_err_t board_hardware_init(void) {
    ESP_LOGI(TAG, "Initializing hardware subsystems for SKU 28141...");

    // 1. Initialize shared I2C bus
    ESP_RETURN_ON_ERROR(i2c_master_init(), TAG, "I2C init failed");

    // 2. Initialize TWAI CAN controller
    ESP_RETURN_ON_ERROR(board_twai_init(), TAG, "CAN init failed");

    // 3. Turn on LCD Backlight
    board_set_backlight(100);

    ESP_LOGI(TAG, "Hardware initialization complete.");
    return ESP_OK;
}
