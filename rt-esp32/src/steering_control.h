#pragma once
#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"
namespace rt {
enum class SteerState : uint8_t { BOOT_WAIT, LISTEN_SYNC, ACTIVE, FAULT };
class SteeringControl {
public:
    void init() { m_state=SteerState::BOOT_WAIT; m_timer=0; m_active_angle=0; m_roll=0; }
    SteerState state() const { return m_state; }
    bool tick(int16_t ses_angle_raw, can::VcuSesReq& out) {
        constexpr int kBootWaitTicks = 25;  // 50 Hz * 500 ms (kSteerBootWaitMs)
        switch(m_state) {
        case SteerState::BOOT_WAIT:
            if(++m_timer>=kBootWaitTicks){m_state=SteerState::LISTEN_SYNC;m_timer=0;}
            return false;
        case SteerState::LISTEN_SYNC:
            if(ses_angle_raw==INT16_MIN) return false;
            m_active_angle=ses_angle_raw; m_state=SteerState::ACTIVE;
            build_and_send(out);
            return true;
        case SteerState::ACTIVE:
            build_and_send(out);
            return true;
        case SteerState::FAULT: return false;
        }
        return false;
    }
    void set_target(int32_t angle_mdeg) { m_active_angle=int16_t(angle_mdeg/100); }
private:
    void build_and_send(can::VcuSesReq& out) {
        out.align_enable=1;out.control_enable=1;
        out.target_angle=m_active_angle;out.target_speed=kTargetSpeed;
        out.roll_cnt_enable=1;out.checksum_enable=1;
        out.rolling_counter=m_roll;m_roll=(m_roll+1)&kRollCounterMask;
    }
    static constexpr int kTargetSpeed     = 100; // SYNTREE EPS-C steering speed
    static constexpr int kRollCounterMask = 0x0F;
    SteerState m_state;
    int        m_timer         = 0;
    int16_t    m_active_angle  = 0;
    uint8_t    m_roll          = 0;
};
}
