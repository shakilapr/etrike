/**
 * @file ui_bridge.h
 * @brief Data binding bridge connecting decoded CAN telemetry to LVGL UI widgets
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "can_decoder.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UNIT_KMH = 0,
    UNIT_MPH = 1,
} speed_unit_t;

/**
 * @brief Initialize UI widgets, load XML views, and configure styles.
 */
void ui_bridge_init(void);

/**
 * @brief Update all UI widgets with the latest telemetry snapshot.
 * @param t Telemetry snapshot.
 */
void ui_bridge_update_telemetry(const vehicle_telemetry_t *t);

/**
 * @brief Switch active speed unit (KM/H vs MPH).
 */
void ui_bridge_set_speed_unit(speed_unit_t unit);

/**
 * @brief Get currently selected speed unit.
 */
speed_unit_t ui_bridge_get_speed_unit(void);

/**
 * @brief Switch active view (0: Cockpit HUD, 1: Diagnostics, 2: CAN Log).
 */
void ui_bridge_switch_screen(uint8_t screen_id);

#ifdef __cplusplus
}
#endif
