#pragma once
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include "config.h"
#include "can/can_protocol.h"
namespace rt {
struct ResolvedSetpoint { int32_t speed; int32_t steer_mdeg; uint8_t gear; bool valid, reversing; };
inline ResolvedSetpoint physics_resolve(const can::HostDriveCmd& cmd) {
    ResolvedSetpoint r{};
    float v_ms=cmd.speed_mmps/1000.0f, w_rs=cmd.yaw_rate_mrad_s/1000.0f;
    r.speed=cmd.speed_mmps;
    if (r.speed>kMaxSpeedFwdMmps) r.speed=kMaxSpeedFwdMmps;
    if (r.speed<-kMaxSpeedRevMmps) r.speed=-kMaxSpeedRevMmps;
    r.reversing=r.speed<0;
    r.gear=(r.speed>0)?1:(r.speed<0?3:0); // D,N,R
    float abs_v=fabs(v_ms);
    if (abs_v>float(kLowSpeedThreshMmps)/1000.0f) {
        r.steer_mdeg=int32_t(atan2(kWheelbaseMM/1000.0f*w_rs,abs_v)*180000.0f/M_PI);
        r.valid=true;
    } else { r.steer_mdeg=0; r.valid=false; }
    // Dynamic clamp (placeholder: fixed 40deg clamp)
    float limit=kSteerHardLimitDeg*1000.0f;
    if (r.steer_mdeg>limit) r.steer_mdeg=limit;
    if (r.steer_mdeg<-limit) r.steer_mdeg=-limit;
    return r;
}
}
