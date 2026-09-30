/**
 * @file board_waveshare_43b.h
 * @brief Board Support Package for Waveshare ESP32-S3-Touch-LCD-4.3B-BOX (SKU: 28141)
 *
 * Hardware features:
 * - 4.3" 800x480 IPS display (ST7262 RGB interface)
 * - Capacitive Touch GT911 (I2C on GPIO8/9)
 * - Onboard TJA1051 CAN transceiver (TWAI TX: GPIO15, RX: GPIO16)
 * - CH422G I/O expander (LCD backlight/reset, DI/DO)
 * - PCF85063A Hardware RTC (I2C GPIO8/9)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Display Dimensions */
#define LCD_H_RES              800
#define LCD_V_RES              480

/* TWAI / CAN Bus Pin Configuration */
#define BOARD_CAN_TX_PIN       GPIO_NUM_15
#define BOARD_CAN_RX_PIN       GPIO_NUM_16
#define BOARD_CAN_BAUDRATE_KBPS 500

/* Shared I2C Bus (Touch GT911, RTC PCF85063, Expander CH422G) */
#define BOARD_I2C_SDA_PIN      GPIO_NUM_8
#define BOARD_I2C_SCL_PIN      GPIO_NUM_9
#define BOARD_I2C_PORT         I2C_NUM_0
#define BOARD_I2C_FREQ_HZ      400000

/* Touch Controller (GT911) */
#define BOARD_TOUCH_INT_PIN    GPIO_NUM_4

/* RGB LCD Interface GPIO Mappings for ST7262 */
#define LCD_PIN_PCLK           GPIO_NUM_7
#define LCD_PIN_DE             GPIO_NUM_5
#define LCD_PIN_VSYNC          GPIO_NUM_3
#define LCD_PIN_HSYNC          GPIO_NUM_46

/* RGB565 Data Lines */
#define LCD_PIN_DATA0          GPIO_NUM_14 // B0
#define LCD_PIN_DATA1          GPIO_NUM_38 // B1
#define LCD_PIN_DATA2          GPIO_NUM_18 // B2
#define LCD_PIN_DATA3          GPIO_NUM_17 // B3
#define LCD_PIN_DATA4          GPIO_NUM_10 // B4
#define LCD_PIN_DATA5          GPIO_NUM_39 // G0
#define LCD_PIN_DATA6          GPIO_NUM_0  // G1
#define LCD_PIN_DATA7          GPIO_NUM_45 // G2
#define LCD_PIN_DATA8          GPIO_NUM_48 // G3
#define LCD_PIN_DATA9          GPIO_NUM_47 // G4
#define LCD_PIN_DATA10         GPIO_NUM_21 // G5
#define LCD_PIN_DATA11         GPIO_NUM_1  // R0
#define LCD_PIN_DATA12         GPIO_NUM_2  // R1
#define LCD_PIN_DATA13         GPIO_NUM_42 // R2
#define LCD_PIN_DATA14         GPIO_NUM_41 // R3
#define LCD_PIN_DATA15         GPIO_NUM_40 // R4

/* RS485 Interface */
#define BOARD_RS485_RX_PIN     GPIO_NUM_43
#define BOARD_RS485_TX_PIN     GPIO_NUM_44

/* MicroSD Card (SPI) */
#define BOARD_SD_MOSI_PIN      GPIO_NUM_11
#define BOARD_SD_CLK_PIN       GPIO_NUM_12
#define BOARD_SD_MISO_PIN      GPIO_NUM_13

/**
 * @brief Initialize all board hardware peripherals (I2C, CH422G, ST7262 RGB LCD, GT911 Touch).
 */
esp_err_t board_hardware_init(void);

/**
 * @brief Initialize TWAI (CAN 2.0) interface on GPIO15 (TX) and GPIO16 (RX) at 500 kbps.
 */
esp_err_t board_twai_init(void);

/**
 * @brief Read isolated digital input states (DI0, DI1) via CH422G expander.
 * @param di0 Pointer to store DI0 boolean state (e.g. ignition on).
 * @param di1 Pointer to store DI1 boolean state.
 */
esp_err_t board_read_isolated_inputs(bool *di0, bool *di1);

/**
 * @brief Control isolated open-drain digital outputs (DO0, DO1).
 * @param do0 Set state for DO0 (alarm/buzzer).
 * @param do1 Set state for DO1 (relay).
 */
esp_err_t board_set_isolated_outputs(bool do0, bool do1);

/**
 * @brief Set LCD backlight brightness (0 - 100%).
 */
esp_err_t board_set_backlight(uint8_t percent);

#ifdef __cplusplus
}
#endif
