#pragma once
// Signal lights + handlebar switches + brake OR logic. Architecture.md §8.6.
#include <cstdint>
#include "config.h"
namespace sys {
struct LightState { bool left,right,brake,head; };
struct LightOutputs { bool left_lamp,right_lamp,brake_lamp,head_lamp; };
class LightControl {
public:
    void init() { m_blink_timer=0;m_blink_on=false;m_left_on=false;m_right_on=false;m_head_on=false; }
    // Call @ 20 Hz. mode: current. lever: brake lever. light_bits: from 0x302 CAN. switches: L/R/head pressed.
    LightOutputs tick(can::Mode mode, bool lever, uint8_t light_bits, bool sw_L, bool sw_R, bool sw_head) {
        LightOutputs out={false,false,false,false};
        if (mode==can::Mode::Estop) { out.brake_lamp=true; return out; }
        // Turn signals
        bool can_L=light_bits&1, can_R=light_bits&2;
        bool want_L=(mode==can::Mode::Manual)?m_left_on:can_L;
        bool want_R=(mode==can::Mode::Manual)?m_right_on:can_R;
        // Handlebar toggle logic (MANUAL)
        if (mode==can::Mode::Manual) {
            if (sw_L && !m_prev_sw_L) m_left_on=!m_left_on;
            if (sw_R && !m_prev_sw_R) m_right_on=!m_right_on;
            if (sw_head && !m_prev_sw_H) m_head_on=!m_head_on;
        }
        m_prev_sw_L=sw_L;m_prev_sw_R=sw_R;m_prev_sw_H=sw_head;
        // Blink
        m_blink_timer=(m_blink_timer+1)%10; // 500ms @ 20Hz = 10 ticks
        if (m_blink_timer==0) m_blink_on=!m_blink_on;
        out.left_lamp=want_L&&m_blink_on; out.right_lamp=want_R&&m_blink_on;
        // Headlight
        out.head_lamp=(mode==can::Mode::Manual)?m_head_on:((light_bits>>3)&1);
        // Brake OR logic
        out.brake_lamp=lever||(mode==can::Mode::Estop)||((light_bits>>2)&1);
        return out;
    }
private:
    int m_blink_timer=0;bool m_blink_on=false;
    bool m_left_on=false,m_right_on=false,m_head_on=false;
    bool m_prev_sw_L=true,m_prev_sw_R=true,m_prev_sw_H=true;
};
}
