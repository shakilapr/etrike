#pragma once
// Signal lights + handlebar switches + brake OR logic. Architecture.md §8.6.
#include <cstdint>
#include "config.h"
namespace sys {
// CAN 0x302 light command bit positions
constexpr uint8_t kLightBitLeftTurn  = 1u << 0;
constexpr uint8_t kLightBitRightTurn = 1u << 1;
constexpr uint8_t kLightBitBrake     = 1u << 2;
constexpr uint8_t kLightBitHeadlight = 1u << 3;

struct LightOutputs { bool left_lamp, right_lamp, brake_lamp, head_lamp; };
class LightControl {
public:
    void init() {
        m_blink_timer = 0;
        m_blink_on    = false;
        m_left_on     = false;
        m_right_on    = false;
        m_head_on     = false;
    }
    // Call @ 20 Hz. mode: current. lever: brake lever. light_bits: from 0x302 CAN.
    // switches: L/R/head pressed (active-low, pull-up).
    // seb_braking: true when SEB stroke > 0mm (catches SEB-internal braking from CAN cmds).
    LightOutputs tick(can::Mode mode, bool lever, uint8_t light_bits,
                      bool sw_L, bool sw_R, bool sw_head,
                      bool seb_braking = false) {
        LightOutputs out = {false, false, false, false};
        if (mode == can::Mode::Estop) { out.brake_lamp = true; return out; }

        // Turn signals: MANUAL uses handlebar toggle, AUTO uses CAN bits
        bool want_L = (mode == can::Mode::Manual) ? m_left_on
                                                   : bool(light_bits & kLightBitLeftTurn);
        bool want_R = (mode == can::Mode::Manual) ? m_right_on
                                                   : bool(light_bits & kLightBitRightTurn);

        // Handlebar toggle logic (MANUAL only)
        if (mode == can::Mode::Manual) {
            if (sw_L && !m_prev_sw_L) m_left_on  = !m_left_on;
            if (sw_R && !m_prev_sw_R) m_right_on = !m_right_on;
            if (sw_head && !m_prev_sw_H) m_head_on = !m_head_on;
        }
        m_prev_sw_L = sw_L;
        m_prev_sw_R = sw_R;
        m_prev_sw_H = sw_head;

        // Blink at 500ms on/off (10 ticks @ 20 Hz)
        m_blink_timer = (m_blink_timer + 1) % 10;
        if (m_blink_timer == 0) m_blink_on = !m_blink_on;
        out.left_lamp  = want_L && m_blink_on;
        out.right_lamp = want_R && m_blink_on;

        // Headlight: MANUAL uses toggle, AUTO uses CAN bit
        out.head_lamp = (mode == can::Mode::Manual)
                            ? m_head_on
                            : bool(light_bits & kLightBitHeadlight);

        // Brake OR logic: lever OR ESTOP OR CAN brake bit OR SEB actively braking
        out.brake_lamp = lever || (mode == can::Mode::Estop)
                         || bool(light_bits & kLightBitBrake)
                         || seb_braking;

        return out;
    }
private:
    int   m_blink_timer = 0;
    bool  m_blink_on    = false;
    bool  m_left_on     = false;
    bool  m_right_on    = false;
    bool  m_head_on     = false;
    bool  m_prev_sw_L   = true;
    bool  m_prev_sw_R   = true;
    bool  m_prev_sw_H   = true;
};
}
