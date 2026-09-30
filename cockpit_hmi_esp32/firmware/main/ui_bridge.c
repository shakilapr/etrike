/**
 * @file ui_bridge.c
 * @brief Telemetry-to-LVGL UI data binding implementation
 */

#include "ui_bridge.h"
#include <stdio.h>
#include <math.h>

static speed_unit_t s_speed_unit = UNIT_KMH;
static uint8_t s_current_screen = 0;

/* Widget Handles */
static lv_obj_t *s_screen_cockpit = NULL;
static lv_obj_t *s_screen_diag = NULL;

static lv_obj_t *s_lbl_speed_val = NULL;
static lv_obj_t *s_lbl_speed_unit = NULL;
static lv_obj_t *s_lbl_target_speed = NULL;
static lv_obj_t *s_lbl_mode_title = NULL;
static lv_obj_t *s_lbl_mode_sub = NULL;

static lv_obj_t *s_lbl_steer_act = NULL;
static lv_obj_t *s_lbl_steer_cmd = NULL;
static lv_obj_t *s_bar_steer = NULL;
static lv_obj_t *s_marker_steer_cmd = NULL;
static lv_obj_t *s_lbl_eps_current = NULL;
static lv_obj_t *s_lbl_eps_temp = NULL;
static lv_obj_t *s_lbl_yaw_rate = NULL;

static lv_obj_t *s_lbl_brake_kpa = NULL;
static lv_obj_t *s_bar_brake = NULL;
static lv_obj_t *s_lbl_brake_temp = NULL;
static lv_obj_t *s_lbl_brake_status = NULL;

static lv_obj_t *s_lbl_motor_rpm = NULL;
static lv_obj_t *s_bar_throttle = NULL;
static lv_obj_t *s_lbl_throttle_pct = NULL;

static lv_obj_t *s_lbl_bus_hz = NULL;
static lv_obj_t *s_lbl_can_tec = NULL;
static lv_obj_t *s_lbl_can_rec = NULL;
static lv_obj_t *s_lbl_can_state = NULL;
static lv_obj_t *s_lbl_total_frames = NULL;

/* Telltale Objects */
static lv_obj_t *s_turn_left_obj = NULL;
static lv_obj_t *s_turn_right_obj = NULL;
static lv_obj_t *s_tt_park = NULL;
static lv_obj_t *s_tt_brake = NULL;
static lv_obj_t *s_tt_hold = NULL;
static lv_obj_t *s_tt_ready = NULL;
static lv_obj_t *s_tt_eps = NULL;
static lv_obj_t *s_tt_estop = NULL;
static lv_obj_t *s_tt_temp = NULL;
static lv_obj_t *s_tt_can = NULL;
static lv_obj_t *s_tt_highbeam = NULL;
static lv_obj_t *s_tt_hazard = NULL;

/* Gear Buttons */
static lv_obj_t *s_gear_btns[5] = {NULL};

/* Overlays */
static lv_obj_t *s_estop_overlay = NULL;

void ui_bridge_init(void) {
    // When using LVGL Pro XML runtime or exported C code:
    // lv_xml_load_file("cockpit_main.xml");
    // Or initialize native fallback layout if XML runtime is compiled to C
    s_screen_cockpit = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen_cockpit, lv_color_hex(0x050505), 0);

    // Speed value hero label
    s_lbl_speed_val = lv_label_create(s_screen_cockpit);
    lv_label_set_text(s_lbl_speed_val, "0");
    lv_obj_align(s_lbl_speed_val, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_text_color(s_lbl_speed_val, lv_color_hex(0xFFFFFF), 0);

    // Speed unit label
    s_lbl_speed_unit = lv_label_create(s_screen_cockpit);
    lv_label_set_text(s_lbl_speed_unit, "KM/H");
    lv_obj_align(s_lbl_speed_unit, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_set_style_text_color(s_lbl_speed_unit, lv_color_hex(0xA3A3A3), 0);

    // Target Speed label
    s_lbl_target_speed = lv_label_create(s_screen_cockpit);
    lv_label_set_text(s_lbl_target_speed, "CMD: 0.0 KM/H");
    lv_obj_align(s_lbl_target_speed, LV_ALIGN_TOP_MID, 0, 205);
    lv_obj_set_style_text_color(s_lbl_target_speed, lv_color_hex(0x22D3EE), 0);

    // Load Cockpit screen as active
    lv_screen_load(s_screen_cockpit);
}

void ui_bridge_set_speed_unit(speed_unit_t unit) {
    s_speed_unit = unit;
    if (s_lbl_speed_unit) {
        lv_label_set_text(s_lbl_speed_unit, unit == UNIT_MPH ? "MPH" : "KM/H");
    }
}

speed_unit_t ui_bridge_get_speed_unit(void) {
    return s_speed_unit;
}

void ui_bridge_switch_screen(uint8_t screen_id) {
    s_current_screen = screen_id;
    if (screen_id == 0 && s_screen_cockpit) {
        lv_screen_load(s_screen_cockpit);
    } else if (screen_id == 1 && s_screen_diag) {
        lv_screen_load(s_screen_diag);
    }
}

void ui_bridge_update_telemetry(const vehicle_telemetry_t *t) {
    if (!t) return;

    char buf[64];

    // 1. Primary Speed Display
    float display_speed = (s_speed_unit == UNIT_MPH) ? (t->speed_act_kmh * 0.621371f) : t->speed_act_kmh;
    float display_cmd_speed = (s_speed_unit == UNIT_MPH) ? (t->speed_cmd_kmh * 0.621371f) : t->speed_cmd_kmh;

    if (s_lbl_speed_val) {
        snprintf(buf, sizeof(buf), "%d", (int)roundf(display_speed));
        lv_label_set_text(s_lbl_speed_val, buf);
    }

    if (s_lbl_target_speed) {
        snprintf(buf, sizeof(buf), "CMD: %.1f %s", display_cmd_speed, s_speed_unit == UNIT_MPH ? "MPH" : "KM/H");
        lv_label_set_text(s_lbl_target_speed, buf);
    }

    // 2. Drive Mode
    if (s_lbl_mode_title) {
        if (t->mode == VEHICLE_MODE_AUTO) {
            lv_label_set_text(s_lbl_mode_title, "AUTONOMOUS DRIVE");
            lv_obj_set_style_text_color(s_lbl_mode_title, lv_color_hex(0x22D3EE), 0);
        } else if (t->mode == VEHICLE_MODE_ESTOP || t->estop_active) {
            lv_label_set_text(s_lbl_mode_title, "EMERGENCY STOP");
            lv_obj_set_style_text_color(s_lbl_mode_title, lv_color_hex(0xF43F5E), 0);
        } else {
            lv_label_set_text(s_lbl_mode_title, "MANUAL OVERRIDE");
            lv_obj_set_style_text_color(s_lbl_mode_title, lv_color_hex(0xF59E0B), 0);
        }
    }

    // 3. Steering Angle & Bi-directional Bar
    if (s_lbl_steer_act) {
        snprintf(buf, sizeof(buf), "%s%.1f°", t->steer_act_deg > 0 ? "+" : "", t->steer_act_deg);
        lv_label_set_text(s_lbl_steer_act, buf);
    }
    if (s_lbl_steer_cmd) {
        snprintf(buf, sizeof(buf), "CMD: %.1f°", t->steer_cmd_deg);
        lv_label_set_text(s_lbl_steer_cmd, buf);
    }
    if (s_bar_steer) {
        lv_bar_set_value(s_bar_steer, (int32_t)t->steer_act_deg, LV_ANIM_OFF);
    }

    // 4. EPS Diagnostics
    if (s_lbl_eps_current) {
        snprintf(buf, sizeof(buf), "EPS: %.1fA", t->steer_current_a);
        lv_label_set_text(s_lbl_eps_current, buf);
    }
    if (s_lbl_eps_temp) {
        snprintf(buf, sizeof(buf), "ECU: %d°C", t->steer_temp_c);
        lv_label_set_text(s_lbl_eps_temp, buf);
    }

    // 5. Yaw Rate
    if (s_lbl_yaw_rate) {
        snprintf(buf, sizeof(buf), "%d mrad/s", t->yaw_rate_mrad_s);
        lv_label_set_text(s_lbl_yaw_rate, buf);
    }

    // 6. Hydraulic Brake
    if (s_lbl_brake_kpa) {
        snprintf(buf, sizeof(buf), "%.0f kPa", t->brake_act_kpa);
        lv_label_set_text(s_lbl_brake_kpa, buf);
    }
    if (s_bar_brake) {
        lv_bar_set_value(s_bar_brake, (int32_t)t->brake_act_kpa, LV_ANIM_OFF);
    }
    if (s_lbl_brake_temp) {
        snprintf(buf, sizeof(buf), "ECU: %d°C", t->brake_temp_c);
        lv_label_set_text(s_lbl_brake_temp, buf);
    }
    if (s_lbl_brake_status) {
        lv_label_set_text(s_lbl_brake_status, t->brake_fault ? "FAULT" : "NORMAL");
        lv_obj_set_style_text_color(s_lbl_brake_status, t->brake_fault ? lv_color_hex(0xF43F5E) : lv_color_hex(0x34D399), 0);
    }

    // 7. Powertrain (Motor RPM & Throttle)
    if (s_lbl_motor_rpm) {
        snprintf(buf, sizeof(buf), "%d RPM", t->motor_rpm);
        lv_label_set_text(s_lbl_motor_rpm, buf);
    }
    if (s_bar_throttle) {
        lv_bar_set_value(s_bar_throttle, t->throttle_percent, LV_ANIM_OFF);
    }
    if (s_lbl_throttle_pct) {
        snprintf(buf, sizeof(buf), "%d%% DEMAND", t->throttle_percent);
        lv_label_set_text(s_lbl_throttle_pct, buf);
    }

    // 8. CAN Telemetry Counters
    if (s_lbl_bus_hz) {
        snprintf(buf, sizeof(buf), "%.1f Hz", t->bus_rate_hz);
        lv_label_set_text(s_lbl_bus_hz, buf);
    }
    if (s_lbl_can_tec) {
        snprintf(buf, sizeof(buf), "TEC: %d", t->twai_tec);
        lv_label_set_text(s_lbl_can_tec, buf);
    }
    if (s_lbl_can_rec) {
        snprintf(buf, sizeof(buf), "REC: %d", t->twai_rec);
        lv_label_set_text(s_lbl_can_rec, buf);
    }
    if (s_lbl_total_frames) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)t->total_frames);
        lv_label_set_text(s_lbl_total_frames, buf);
    }

    // 9. Full-Screen Emergency Strobe Overlay
    if (s_estop_overlay) {
        if (t->estop_active || t->mode == VEHICLE_MODE_ESTOP) {
            lv_obj_clear_flag(s_estop_overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_estop_overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
