#pragma once
// Physical wheel-speed EGAS monitor (issue #3) — OPTIONAL capability.
//
// The current vehicle has NO wheel encoder: SYS supervises the command path via
// the 0x206 MTR_MOTOR_FBK echo (requested vs applied) and MTR's own safety
// gates. When a wheel encoder is fitted to RT (ETRIKE_RT_ENCODERS=1) RT
// publishes 0x122 RT_WHEEL_SPEED_STS (measured_speed_mmps + sensor_state). This
// monitor compares the *physical* measured speed against the SYS requested speed
// to catch what the command-path check cannot: a wheel that moves when it must
// not (runaway), moves the wrong way (direction mismatch), or fails to move when
// commanded (stall).
//
// Gating: the caller only enables it when the vehicle is known to have the
// sensor (sys::kPhysicalWheelSensorInstalled) and only VALID + fresh frames are
// used. A FAULT/ACQUIRING/missing stream on an encoder-equipped vehicle must
// NOT be silently treated as "no sensor" — the caller escalates that separately.
//
// The class is pure C++ so it can be unit-tested without FreeRTOS.

#include <cstdint>

namespace sys {

enum class PhysicalEgasVerdict : uint8_t {
    OK = 0,
    Runaway = 1,          // moving while not commanded
    DirectionMismatch = 2,// moving opposite to the command
    Stall = 3,            // commanded but no physical motion
};

// Detection tuning (10 Hz control cadence).
constexpr int  kPhysicalEgasTripFrames      = 5;    // persist this many ticks before firing
constexpr int  kPhysicalEgasRunawayMmps     = 400;  // sustained wheel speed while ~stopped
constexpr int  kPhysicalEgasCmdZeroMmps     = 50;   // command magnitude treated as "stopped"
constexpr int  kPhysicalEgasDirCheckMmps    = 200;  // require this motion to compare direction
constexpr int  kPhysicalEgasStallMmps       = 50;   // below this = not physically moving
constexpr uint32_t kPhysicalEgasFreshMs     = 300;  // 0x122 considered fresh

struct PhysicalEgasInput {
    bool     sensor_installed = false;  // vehicle fitted with a wheel encoder
    bool     frame_fresh      = false;  // 0x122 within kPhysicalEgasFreshMs
    uint8_t  sensor_state     = 0;      // 0 NOT_INSTALLED,1 ACQUIRING,2 VALID,3 FAULT
    int16_t  measured_mmps    = 0;
    int16_t  commanded_mmps   = 0;      // signed SYS requested speed (R = negative)
};

class PhysicalEgasMonitor {
public:
    void reset() { runaway_ = 0; dir_ = 0; stall_ = 0; }

    PhysicalEgasVerdict update(const PhysicalEgasInput& in) {
        // Physical EGAS only exists when the sensor is fitted and reporting VALID
        // fresh frames. Everything else is "no signal" — the caller decides how to
        // treat a missing/FAULT stream on an encoder-equipped vehicle.
        if (!in.sensor_installed || !in.frame_fresh
            || in.sensor_state != 2 /*VALID*/) {
            reset();
            return PhysicalEgasVerdict::OK;
        }

        const int32_t cmd = in.commanded_mmps;
        const int32_t cmd_mag = cmd >= 0 ? cmd : -cmd;
        const int32_t m = in.measured_mmps;
        const int32_t m_mag = m >= 0 ? m : -m;

        if (cmd_mag <= kPhysicalEgasCmdZeroMmps) {
            // Not commanded to move: any sustained motion is a runaway.
            runaway_ = (m_mag > kPhysicalEgasRunawayMmps) ? uint8_t(runaway_ + 1) : 0;
            dir_ = 0; stall_ = 0;
            return (runaway_ >= kPhysicalEgasTripFrames)
                ? PhysicalEgasVerdict::Runaway : PhysicalEgasVerdict::OK;
        }

        // Commanded to move.
        runaway_ = 0;
        if (m_mag > kPhysicalEgasDirCheckMmps) {
            // Moving enough to judge direction.
            stall_ = 0;
            const bool same_sign = (cmd >= 0) == (m >= 0);
            dir_ = same_sign ? 0 : uint8_t(dir_ + 1);
            return (dir_ >= kPhysicalEgasTripFrames)
                ? PhysicalEgasVerdict::DirectionMismatch : PhysicalEgasVerdict::OK;
        }
        if (m_mag <= kPhysicalEgasStallMmps) {
            // Commanded but the wheel is not physically moving.
            stall_ = uint8_t(stall_ + 1);
            return (stall_ >= kPhysicalEgasTripFrames)
                ? PhysicalEgasVerdict::Stall : PhysicalEgasVerdict::OK;
        }
        stall_ = 0;
        dir_ = 0;
        return PhysicalEgasVerdict::OK;
    }

private:
    uint8_t runaway_ = 0;
    uint8_t dir_ = 0;
    uint8_t stall_ = 0;
};

}  // namespace sys
