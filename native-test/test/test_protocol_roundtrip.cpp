/*
 * test_protocol_roundtrip.cpp
 *
 * Pack -> unpack roundtrip tests for EVERY struct in can_protocol.h that
 * has both to_frame() and from_frame().
 *
 * Tests cover:
 *   - Frame metadata (id, dlc)
 *   - All fields roundtrip exactly
 *   - i24 sign extension (HostDriveCmd yaw: positive, negative, zero, edge)
 *   - Boundary values for each numeric field
 *   - Bool field encoding/decoding (SysSafetySts, HostLightCmd, SysDiagRpt)
 *   - Bitfield packing (VcuSesReq, VcuSebReq)
 *   - SYNTREE LE pack/unpack roundtrips
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <climits>

#include "can/can_protocol.h"   // provides Frame, all struct types
// NOTE: can_driver.h is intentionally NOT included here. It pulls in
//       ESP-IDF macros (TWAI_GENERAL_CONFIG_DEFAULT_V2) that fail on
//       host builds. Frame is defined in can_protocol.h and is sufficient.

static int failures = 0;
#define CHECK(cond, msg)                                 \
    do {                                                 \
        if (!(cond)) {                                   \
            printf("  FAIL: %s\n", msg);                 \
            failures++;                                  \
        }                                                \
    } while (0)

/* ---- Forward: VcuSesReq/VcuSebReq have pack/unpack helpers ---- */

static uint8_t compute_xor_checksum(const uint8_t raw[8]) {
    uint8_t cksum = 0;
    for (int i = 0; i < 7; ++i) cksum ^= raw[i];
    return cksum ^ 0xFF;
}

/* ===================================================================
 *  1) SysSafetySts (0x011)
 * =================================================================== */

static void test_sys_safety_sts() {
    printf("  SysSafetySts ...\n");

    // Nominal: estop active, hb ok, all lights on
    {
        can::SysSafetySts s{true, true, 0x0F};
        can::Frame f{};
        s.to_frame(f);
        CHECK(f.id  == 0x011, "SysSafetySts id (0x011)");
        CHECK(f.dlc == 3,      "SysSafetySts dlc (3)");
        auto r = can::SysSafetySts::from_frame(f);
        CHECK(r.estop_active == true,  "SysSafetySts estop_active true");
        CHECK(r.heartbeat_ok == true,  "SysSafetySts heartbeat_ok true");
        CHECK(r.light_state  == 0x0F,  "SysSafetySts light_state 0x0F");
    }

    // All false/zero
    {
        can::SysSafetySts s{};
        can::Frame f{};
        s.to_frame(f);
        auto r = can::SysSafetySts::from_frame(f);
        CHECK(r.estop_active == false, "SysSafetySts estop_active false");
        CHECK(r.heartbeat_ok == false, "SysSafetySts heartbeat_ok false");
        CHECK(r.light_state  == 0,     "SysSafetySts light_state 0");
    }

    // Estop only, specific lights (left+brake)
    {
        can::SysSafetySts s{true, false, 0x05};
        can::Frame f{};
        s.to_frame(f);
        auto r = can::SysSafetySts::from_frame(f);
        CHECK(r.estop_active == true,  "SysSafetySts estop_active standalone");
        CHECK(r.heartbeat_ok == false, "SysSafetySts hb_ok standalone");
        CHECK(r.light_state  == 0x05,  "SysSafetySts light_state 0x05 (left+brake)");
    }
}

/* ===================================================================
 *  2) SysModeCmd (0x110)
 * =================================================================== */

static void test_sys_mode_cmd() {
    printf("  SysModeCmd ...\n");

    // Manual
    {
        can::SysModeCmd m{uint8_t(can::Mode::Manual)};
        can::Frame f{}; m.to_frame(f);
        CHECK(f.id  == 0x110, "SysModeCmd id (0x110)");
        CHECK(f.dlc == 1,     "SysModeCmd dlc (1)");
        auto r = can::SysModeCmd::from_frame(f);
        CHECK(r.mode == uint8_t(can::Mode::Manual), "SysModeCmd mode Manual");
    }

    // Auto
    {
        can::SysModeCmd m{uint8_t(can::Mode::Auto)};
        can::Frame f{}; m.to_frame(f);
        auto r = can::SysModeCmd::from_frame(f);
        CHECK(r.mode == uint8_t(can::Mode::Auto), "SysModeCmd mode Auto");
    }

    // Estop
    {
        can::SysModeCmd m{uint8_t(can::Mode::Estop)};
        can::Frame f{}; m.to_frame(f);
        auto r = can::SysModeCmd::from_frame(f);
        CHECK(r.mode == uint8_t(can::Mode::Estop), "SysModeCmd mode Estop");
    }

    // Boundary: all ones (0xFF)
    {
        can::SysModeCmd m{0xFF};
        can::Frame f{}; m.to_frame(f);
        auto r = can::SysModeCmd::from_frame(f);
        CHECK(r.mode == 0xFF, "SysModeCmd boundary 0xFF");
    }
}

/* ===================================================================
 *  3) SysThrottleSts (0x120)
 * =================================================================== */

static void test_sys_throttle_sts() {
    printf("  SysThrottleSts ...\n");

    // Zero
    {
        can::SysThrottleSts t{0};
        can::Frame f{}; t.to_frame(f);
        CHECK(f.id  == 0x120, "SysThrottleSts id (0x120)");
        CHECK(f.dlc == 2,     "SysThrottleSts dlc (2)");
        auto r = can::SysThrottleSts::from_frame(f);
        CHECK(r.speed_mmps == 0, "SysThrottleSts speed zero");
    }

    // Positive
    {
        can::SysThrottleSts t{32767};  // max int16
        can::Frame f{}; t.to_frame(f);
        auto r = can::SysThrottleSts::from_frame(f);
        CHECK(r.speed_mmps == 32767, "SysThrottleSts speed max int16");
    }

    // Negative (reverse)
    {
        can::SysThrottleSts t{-32768};  // min int16
        can::Frame f{}; t.to_frame(f);
        auto r = can::SysThrottleSts::from_frame(f);
        CHECK(r.speed_mmps == -32768, "SysThrottleSts speed min int16");
    }

    // Typical forward speed
    {
        can::SysThrottleSts t{1500};
        can::Frame f{}; t.to_frame(f);
        auto r = can::SysThrottleSts::from_frame(f);
        CHECK(r.speed_mmps == 1500, "SysThrottleSts speed 1500");
    }
}

/* ===================================================================
 *  4) SteerDiag (0x310)
 * =================================================================== */

static void test_steer_diag() {
    printf("  SteerDiag ...\n");

    // Nominal
    {
        can::SteerDiag d{450, 0, 1200, 350, 0};
        can::Frame f{}; d.to_frame(f);
        CHECK(f.id  == 0x310, "SteerDiag id (0x310)");
        CHECK(f.dlc == 8,     "SteerDiag dlc (8)");
        auto r = can::SteerDiag::from_frame(f);
        CHECK(r.angle_0_1deg  == 450,   "SteerDiag angle");
        CHECK(r.fault          == 0,    "SteerDiag no fault");
        CHECK(r.motor_current  == 1200, "SteerDiag motor current");
        CHECK(r.ecu_temp       == 350,  "SteerDiag ecu temp");
    }

    // Fault condition + max values
    {
        can::SteerDiag d{-3000, 1, 65535, 65535, 0};
        can::Frame f{}; d.to_frame(f);
        auto r = can::SteerDiag::from_frame(f);
        CHECK(r.angle_0_1deg  == -3000, "SteerDiag angle negative");
        CHECK(r.fault          == 1,    "SteerDiag fault set");
        CHECK(r.motor_current  == 65535,"SteerDiag max motor current");
        CHECK(r.ecu_temp       == 65535,"SteerDiag max ecu temp");
    }
}

/* ===================================================================
 *  5) BrakeDiag (0x311)
 * =================================================================== */

static void test_brake_diag() {
    printf("  BrakeDiag ...\n");

    // Nominal
    {
        can::BrakeDiag d{280, 0, 800, 420, 0};
        can::Frame f{}; d.to_frame(f);
        CHECK(f.id  == 0x311, "BrakeDiag id (0x311)");
        CHECK(f.dlc == 8,     "BrakeDiag dlc (8)");
        auto r = can::BrakeDiag::from_frame(f);
        CHECK(r.pressure_raw  == 280,  "BrakeDiag pressure");
        CHECK(r.fault          == 0,   "BrakeDiag no fault");
        CHECK(r.motor_current == 800,  "BrakeDiag motor current");
        CHECK(r.ecu_temp      == 420,  "BrakeDiag ecu temp");
    }

    // Fault + boundaries
    {
        can::BrakeDiag d{0, 0xFF, 0, 0, 0};
        can::Frame f{}; d.to_frame(f);
        auto r = can::BrakeDiag::from_frame(f);
        CHECK(r.pressure_raw  == 0,    "BrakeDiag zero pressure");
        CHECK(r.fault          == 0xFF,"BrakeDiag all fault bits");
    }
}

/* ===================================================================
 *  6) RtDriveCmd (0x204)
 * =================================================================== */

static void test_rt_drive_cmd() {
    printf("  RtDriveCmd ...\n");

    // Forward with speed
    {
        can::RtDriveCmd c{2000, uint8_t(can::Gear::D)};
        can::Frame f{}; c.to_frame(f);
        CHECK(f.id  == 0x204, "RtDriveCmd id (0x204)");
        CHECK(f.dlc == 5,     "RtDriveCmd dlc (5)");
        auto r = can::RtDriveCmd::from_frame(f);
        CHECK(r.motor_speed_mmps == 2000, "RtDriveCmd speed D");
        CHECK(r.gear == uint8_t(can::Gear::D), "RtDriveCmd gear D");
    }

    // Reverse
    {
        can::RtDriveCmd c{-500, uint8_t(can::Gear::R)};
        can::Frame f{}; c.to_frame(f);
        auto r = can::RtDriveCmd::from_frame(f);
        CHECK(r.motor_speed_mmps == -500, "RtDriveCmd reverse speed");
        CHECK(r.gear == uint8_t(can::Gear::R), "RtDriveCmd gear R");
    }

    // Boundary max positive speed
    {
        can::RtDriveCmd c{3000, uint8_t(can::Gear::S)};
        can::Frame f{}; c.to_frame(f);
        auto r = can::RtDriveCmd::from_frame(f);
        CHECK(r.motor_speed_mmps == 3000, "RtDriveCmd max speed");
        CHECK(r.gear == uint8_t(can::Gear::S), "RtDriveCmd gear S");
    }

    // Neutral, zero speed
    {
        can::RtDriveCmd c{0, uint8_t(can::Gear::N)};
        can::Frame f{}; c.to_frame(f);
        auto r = can::RtDriveCmd::from_frame(f);
        CHECK(r.motor_speed_mmps == 0,   "RtDriveCmd zero speed");
        CHECK(r.gear == uint8_t(can::Gear::N), "RtDriveCmd gear N");
    }
}

/* ===================================================================
 *  7) MtrMotorFbk (0x206)
 * =================================================================== */

static void test_mtr_motor_fbk() {
    printf("  MtrMotorFbk ...\n");

    // Normal forward
    {
        can::MtrMotorFbk m{1500, uint8_t(can::Gear::D), 0x00};
        can::Frame f{}; m.to_frame(f);
        CHECK(f.id  == 0x206, "MtrMotorFbk id (0x206)");
        CHECK(f.dlc == 4,     "MtrMotorFbk dlc (4)");
        auto r = can::MtrMotorFbk::from_frame(f);
        CHECK(r.actual_speed_mmps == 1500, "MtrMotorFbk speed D");
        CHECK(r.gear_state == uint8_t(can::Gear::D), "MtrMotorFbk gear D");
        CHECK(r.fault_flags == 0x00, "MtrMotorFbk no faults");
    }

    // Fault condition
    {
        can::MtrMotorFbk m{0, uint8_t(can::Gear::S), 0x05};
        can::Frame f{}; m.to_frame(f);
        auto r = can::MtrMotorFbk::from_frame(f);
        CHECK(r.actual_speed_mmps == 0, "MtrMotorFbk speed S");
        CHECK(r.gear_state == uint8_t(can::Gear::S), "MtrMotorFbk gear S");
        CHECK(r.fault_flags == 0x05, "MtrMotorFbk faults 0x05");
    }

    // Negative speed (coasting backwards)
    {
        can::MtrMotorFbk m{-100, uint8_t(can::Gear::R), 0x01};
        can::Frame f{}; m.to_frame(f);
        auto r = can::MtrMotorFbk::from_frame(f);
        CHECK(r.actual_speed_mmps == -100, "MtrMotorFbk speed reverse");
        CHECK(r.gear_state == uint8_t(can::Gear::R), "MtrMotorFbk gear R");
        CHECK(r.fault_flags == 0x01, "MtrMotorFbk fault ESTOP");
    }
}

/* ===================================================================
 *  8) RtBrakeCmd (0x205)
 * =================================================================== */

static void test_rt_brake_cmd() {
    printf("  RtBrakeCmd ...\n");

    // Release
    {
        can::RtBrakeCmd b{0};
        can::Frame f{}; b.to_frame(f);
        CHECK(f.id  == 0x205, "RtBrakeCmd id (0x205)");
        CHECK(f.dlc == 4,     "RtBrakeCmd dlc (4)");
        auto r = can::RtBrakeCmd::from_frame(f);
        CHECK(r.brake_pressure_kpa == 0, "RtBrakeCmd release");
    }

    // Moderate braking
    {
        can::RtBrakeCmd b{5000};
        can::Frame f{}; b.to_frame(f);
        auto r = can::RtBrakeCmd::from_frame(f);
        CHECK(r.brake_pressure_kpa == 5000, "RtBrakeCmd 5000 kPa");
    }

    // Max braking
    {
        can::RtBrakeCmd b{20000};
        can::Frame f{}; b.to_frame(f);
        auto r = can::RtBrakeCmd::from_frame(f);
        CHECK(r.brake_pressure_kpa == 20000, "RtBrakeCmd 20000 kPa");
    }

    // Negative (should not happen in practice, but must roundtrip)
    {
        can::RtBrakeCmd b{-1};
        can::Frame f{}; b.to_frame(f);
        auto r = can::RtBrakeCmd::from_frame(f);
        CHECK(r.brake_pressure_kpa == -1, "RtBrakeCmd negative one");
    }
}

/* ===================================================================
 *  9) HostLightCmd (0x302)
 * =================================================================== */

static void test_host_light_cmd() {
    printf("  HostLightCmd ...\n");

    // All on
    {
        can::HostLightCmd l{true, true, true, true};
        can::Frame f{}; l.to_frame(f);
        CHECK(f.id  == 0x302, "HostLightCmd id (0x302)");
        CHECK(f.dlc == 1,     "HostLightCmd dlc (1)");
        CHECK(f.u8_at(0) == 0x0F, "HostLightCmd raw byte all on");
        auto r = can::HostLightCmd::from_frame(f);
        CHECK(r.left_turn   == true, "HostLightCmd left_turn on");
        CHECK(r.right_turn  == true, "HostLightCmd right_turn on");
        CHECK(r.brake_light == true, "HostLightCmd brake_light on");
        CHECK(r.headlight   == true, "HostLightCmd headlight on");
    }

    // All off
    {
        can::HostLightCmd l{};
        can::Frame f{}; l.to_frame(f);
        auto r = can::HostLightCmd::from_frame(f);
        CHECK(r.left_turn   == false, "HostLightCmd left_turn off");
        CHECK(r.right_turn  == false, "HostLightCmd right_turn off");
        CHECK(r.brake_light == false, "HostLightCmd brake_light off");
        CHECK(r.headlight   == false, "HostLightCmd headlight off");
    }

    // Left + brake only
    {
        can::HostLightCmd l{true, false, true, false};
        can::Frame f{}; l.to_frame(f);
        CHECK(f.u8_at(0) == 0x05, "HostLightCmd raw byte left+brake");
        auto r = can::HostLightCmd::from_frame(f);
        CHECK(r.left_turn   == true,  "HostLightCmd left_turn set");
        CHECK(r.right_turn  == false, "HostLightCmd right_turn clear");
        CHECK(r.brake_light == true,  "HostLightCmd brake_light set");
        CHECK(r.headlight   == false, "HostLightCmd headlight clear");
    }

    // Right + headlight only
    {
        can::HostLightCmd l{false, true, false, true};
        can::Frame f{}; l.to_frame(f);
        CHECK(f.u8_at(0) == 0x0A, "HostLightCmd raw byte right+headlight");
        auto r = can::HostLightCmd::from_frame(f);
        CHECK(r.left_turn   == false, "HostLightCmd right+head: left off");
        CHECK(r.right_turn  == true,  "HostLightCmd right+head: right on");
        CHECK(r.brake_light == false, "HostLightCmd right+head: brake off");
        CHECK(r.headlight   == true,  "HostLightCmd right+head: headlight on");
    }
}

/* ===================================================================
 *  10) SysDiagRpt (0x600)
 * =================================================================== */

static void test_sys_diag_rpt() {
    printf("  SysDiagRpt ...\n");

    // Nominal: Auto mode, brake off, hb ok, no estop
    {
        can::SysDiagRpt d{uint8_t(can::Mode::Auto), false, true, false, 128, 0, 0};
        can::Frame f{}; d.to_frame(f);
        CHECK(f.id  == 0x600, "SysDiagRpt id (0x600)");
        CHECK(f.dlc == 8,     "SysDiagRpt dlc (8)");
        auto r = can::SysDiagRpt::from_frame(f);
        CHECK(r.mode          == uint8_t(can::Mode::Auto), "SysDiagRpt mode Auto");
        CHECK(r.brake_engaged == false, "SysDiagRpt brake not engaged");
        CHECK(r.heartbeat_ok  == true,  "SysDiagRpt hb ok");
        CHECK(r.estop_active  == false, "SysDiagRpt no estop");
        CHECK(r.free_heap_kb  == 128,   "SysDiagRpt heap 128");
        CHECK(r.tec           == 0,     "SysDiagRpt tec 0");
        CHECK(r.rec           == 0,     "SysDiagRpt rec 0");
    }

    // Estop active, all indicators set, error counters non-zero
    {
        can::SysDiagRpt d{uint8_t(can::Mode::Estop), true, false, true, 42, 127, 255};
        can::Frame f{}; d.to_frame(f);
        auto r = can::SysDiagRpt::from_frame(f);
        CHECK(r.mode          == uint8_t(can::Mode::Estop), "SysDiagRpt mode Estop");
        CHECK(r.brake_engaged == true,  "SysDiagRpt brake engaged");
        CHECK(r.heartbeat_ok  == false, "SysDiagRpt hb not ok");
        CHECK(r.estop_active  == true,  "SysDiagRpt estop active");
        CHECK(r.free_heap_kb  == 42,    "SysDiagRpt heap 42");
        CHECK(r.tec           == 127,   "SysDiagRpt tec 127");
        CHECK(r.rec           == 255,   "SysDiagRpt rec 255");
    }

    // Boundary: max heap, max counters
    {
        can::SysDiagRpt d{0, false, false, false, 65535, 255, 255};
        can::Frame f{}; d.to_frame(f);
        auto r = can::SysDiagRpt::from_frame(f);
        CHECK(r.free_heap_kb  == 65535, "SysDiagRpt max heap");
        CHECK(r.tec           == 255,   "SysDiagRpt max tec");
        CHECK(r.rec           == 255,   "SysDiagRpt max rec");
    }
}

/* ===================================================================
 *  11) HostDriveCmd (0x300)  -- i24 SIGN EXTENSION TESTS
 * =================================================================== */

static void test_host_drive_cmd() {
    printf("  HostDriveCmd (i24 sign extension) ...\n");

    // Zero yaw
    {
        can::HostDriveCmd c{0, 0, 0};
        can::Frame f{}; c.to_frame(f);
        CHECK(f.id  == 0x300, "HostDriveCmd id (0x300)");
        CHECK(f.dlc == 8,     "HostDriveCmd dlc (8)");
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.speed_mmps      == 0, "HostDriveCmd zero speed");
        CHECK(r.yaw_rate_mrad_s == 0, "HostDriveCmd zero yaw");
        CHECK(r.gear            == 0, "HostDriveCmd gear N");
    }

    // Positive yaw (typical right turn)
    {
        can::HostDriveCmd c{1000, 500, uint8_t(can::Gear::D)};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.speed_mmps      == 1000, "HostDriveCmd speed 1000 D");
        CHECK(r.yaw_rate_mrad_s == 500,  "HostDriveCmd yaw +500");
        CHECK(r.gear == uint8_t(can::Gear::D), "HostDriveCmd gear D");
    }

    // Negative yaw (left turn) -- i24 sign extension
    {
        can::HostDriveCmd c{-500, -1500, uint8_t(can::Gear::R)};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.speed_mmps      == -500,  "HostDriveCmd speed -500 R");
        CHECK(r.yaw_rate_mrad_s == -1500, "HostDriveCmd yaw -1500");
        CHECK(r.gear == uint8_t(can::Gear::R), "HostDriveCmd gear R");
    }

    // i24 boundary: max positive (0x7FFFFF = 8388607)
    {
        int32_t max_pos = 0x7FFFFF;
        can::HostDriveCmd c{0, max_pos, 0};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.yaw_rate_mrad_s == max_pos, "HostDriveCmd yaw max positive (+8388607)");
    }

    // i24 boundary: min negative (0x800000 sign-extended = -8388608)
    {
        // -8388608 is -(2^23). In i24 it's 0x800000.
        can::HostDriveCmd c{0, -8388608, 0};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.yaw_rate_mrad_s == -8388608, "HostDriveCmd yaw min negative (-8388608)");
    }

    // i24 boundary: -1
    {
        can::HostDriveCmd c{0, -1, 0};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.yaw_rate_mrad_s == -1, "HostDriveCmd yaw -1");
    }

    // i24 boundary: +1
    {
        can::HostDriveCmd c{0, 1, 0};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.yaw_rate_mrad_s == 1, "HostDriveCmd yaw +1");
    }

    // Typical host command: moderate speed + yaw
    {
        can::HostDriveCmd c{1500, -800, uint8_t(can::Gear::D)};
        can::Frame f{}; c.to_frame(f);
        auto r = can::HostDriveCmd::from_frame(f);
        CHECK(r.speed_mmps      == 1500, "HostDriveCmd typical speed");
        CHECK(r.yaw_rate_mrad_s == -800, "HostDriveCmd typical yaw -800");
        CHECK(r.gear == uint8_t(can::Gear::D), "HostDriveCmd typical gear D");
    }
}

/* ===================================================================
 *  12) HostBrakeReq (0x301)
 * =================================================================== */

static void test_host_brake_req() {
    printf("  HostBrakeReq ...\n");

    // No braking
    {
        can::HostBrakeReq b{0};
        can::Frame f{}; b.to_frame(f);
        CHECK(f.id  == 0x301, "HostBrakeReq id (0x301)");
        CHECK(f.dlc == 4,     "HostBrakeReq dlc (4)");
        auto r = can::HostBrakeReq::from_frame(f);
        CHECK(r.brake_pressure_kpa == 0, "HostBrakeReq zero");
    }

    // Full brake
    {
        can::HostBrakeReq b{20000};
        can::Frame f{}; b.to_frame(f);
        auto r = can::HostBrakeReq::from_frame(f);
        CHECK(r.brake_pressure_kpa == 20000, "HostBrakeReq 20000");
    }

    // Negative (invalid but must roundtrip)
    {
        can::HostBrakeReq b{-1000};
        can::Frame f{}; b.to_frame(f);
        auto r = can::HostBrakeReq::from_frame(f);
        CHECK(r.brake_pressure_kpa == -1000, "HostBrakeReq negative");
    }
}

/* ===================================================================
 *  13) HostObstacleDist (0x400)
 * =================================================================== */

static void test_host_obstacle_dist() {
    printf("  HostObstacleDist ...\n");

    // Clear path
    {
        can::HostObstacleDist d{0};
        can::Frame f{}; d.to_frame(f);
        CHECK(f.id  == 0x400, "HostObstacleDist id (0x400)");
        CHECK(f.dlc == 4,     "HostObstacleDist dlc (4)");
        auto r = can::HostObstacleDist::from_frame(f);
        CHECK(r.distance_mm == 0, "HostObstacleDist 0 mm");
    }

    // Obstacle at 5 m
    {
        can::HostObstacleDist d{5000};
        can::Frame f{}; d.to_frame(f);
        auto r = can::HostObstacleDist::from_frame(f);
        CHECK(r.distance_mm == 5000, "HostObstacleDist 5000 mm");
    }

    // No reading
    {
        can::HostObstacleDist d{UINT32_MAX};
        can::Frame f{}; d.to_frame(f);
        auto r = can::HostObstacleDist::from_frame(f);
        CHECK(r.distance_mm == UINT32_MAX, "HostObstacleDist UINT32_MAX (no reading)");
    }

    // Large distance
    {
        can::HostObstacleDist d{100000};
        can::Frame f{}; d.to_frame(f);
        auto r = can::HostObstacleDist::from_frame(f);
        CHECK(r.distance_mm == 100000, "HostObstacleDist 100000 mm");
    }
}

/* ===================================================================
 *  14) VcuSesReq (0x169)  -- SYNTREE LE pack/unpack + checksum
 * =================================================================== */

static void test_vcu_ses_req() {
    printf("  VcuSesReq ...\n");

    // Nominal roundtrip
    {
        can::VcuSesReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.target_angle    = 12000;      // non-trivial angle
        r.target_speed    = 400;        // non-trivial speed
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 7;
        r.vehicle_speed   = 25;

        can::Frame f{}; r.to_frame(f);
        CHECK(f.id  == 0x169, "VcuSesReq id (0x169)");
        CHECK(f.dlc == 8,     "VcuSesReq dlc (8)");

        // Verify checksum on wire
        uint8_t cksum = compute_xor_checksum(f.data);
        CHECK(f.data[7] == cksum, "VcuSesReq wire checksum matches XOR^0xFF");

        auto unpacked = can::VcuSesReq::unpack(f.data);
        CHECK(unpacked.align_enable    == r.align_enable,    "VcuSesReq align_enable");
        CHECK(unpacked.control_enable  == r.control_enable,  "VcuSesReq control_enable");
        CHECK(unpacked.target_angle    == r.target_angle,    "VcuSesReq target_angle");
        CHECK(unpacked.roll_cnt_enable == r.roll_cnt_enable, "VcuSesReq roll_cnt_enable");
        CHECK(unpacked.checksum_enable == r.checksum_enable, "VcuSesReq checksum_enable");
        CHECK(unpacked.rolling_counter == r.rolling_counter, "VcuSesReq rolling_counter");
        CHECK(unpacked.vehicle_speed   == r.vehicle_speed,   "VcuSesReq vehicle_speed");
        CHECK(unpacked.checksum        == f.data[7],          "VcuSesReq checksum field == wire");
    }

    // All zeros (straight ahead, no control)
    {
        can::VcuSesReq r{};
        r.target_speed = 328;   // SYNTREE default

        can::Frame f{}; r.to_frame(f);
        auto unpacked = can::VcuSesReq::unpack(f.data);
        CHECK(unpacked.target_angle == 0, "VcuSesReq zero angle");
        CHECK(unpacked.rolling_counter == 0, "VcuSesReq zero roll cnt");
        CHECK(unpacked.vehicle_speed == 0, "VcuSesReq zero vehicle speed");
    }

    // Rolling counter full cycle (0..15)
    for (uint8_t cnt = 0; cnt < 16; ++cnt) {
        can::VcuSesReq r{};
        r.control_enable  = 1;
        r.checksum_enable = 1;
        r.roll_cnt_enable = 1;
        r.rolling_counter = cnt;
        r.target_speed    = 328;

        can::Frame f{}; r.to_frame(f);
        auto unpacked = can::VcuSesReq::unpack(f.data);
        CHECK(unpacked.rolling_counter == cnt, "VcuSesReq rolling counter cycle");
    }
}

/* ===================================================================
 *  15) VcuSebReq (0x7B9)  -- SYNTREE LE pack/unpack + checksum
 * =================================================================== */

static void test_vcu_seb_req() {
    printf("  VcuSebReq ...\n");

    // Stroke mode nominal
    {
        can::VcuSebReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.control_mode    = 0;           // Stroke
        r.auto_brake      = 0;
        r.stroke_req      = 12850;
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 7;

        can::Frame f{}; r.to_frame(f);
        CHECK(f.id  == 0x7B9, "VcuSebReq id (0x7B9)");
        CHECK(f.dlc == 8,     "VcuSebReq dlc (8)");

        uint8_t cksum = compute_xor_checksum(f.data);
        CHECK(f.data[7] == cksum, "VcuSebReq wire checksum matches XOR^0xFF");

        auto unpacked = can::VcuSebReq::unpack(f.data);
        CHECK(unpacked.align_enable    == r.align_enable,    "VcuSebReq align_enable");
        CHECK(unpacked.control_enable  == r.control_enable,  "VcuSebReq control_enable");
        CHECK(unpacked.control_mode    == r.control_mode,    "VcuSebReq control_mode stroke");
        CHECK(unpacked.auto_brake      == r.auto_brake,      "VcuSebReq auto_brake");
        CHECK(unpacked.stroke_req      == r.stroke_req,      "VcuSebReq stroke_req");
        CHECK(unpacked.roll_cnt_enable == r.roll_cnt_enable, "VcuSebReq roll_cnt_enable");
        CHECK(unpacked.checksum_enable == r.checksum_enable, "VcuSebReq checksum_enable");
        CHECK(unpacked.rolling_counter == r.rolling_counter, "VcuSebReq rolling_counter");
        CHECK(unpacked.checksum        == f.data[7],          "VcuSebReq checksum field == wire");
    }

    // Pressure mode
    {
        can::VcuSebReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.control_mode    = 1;           // Pressure
        r.auto_brake      = 0;
        r.pressure_req    = 80;          // 4.0 MPa
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = 3;

        can::Frame f{}; r.to_frame(f);
        auto unpacked = can::VcuSebReq::unpack(f.data);
        CHECK(unpacked.control_mode  == 1,  "VcuSebReq pressure mode");
        CHECK(unpacked.pressure_req  == 80, "VcuSebReq pressure 80");
    }

    // All zeros with default stroke
    {
        can::VcuSebReq r{};
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;

        can::Frame f{}; r.to_frame(f);
        auto unpacked = can::VcuSebReq::unpack(f.data);
        CHECK(unpacked.stroke_req      == 600, "VcuSebReq default stroke 600");
        CHECK(unpacked.rolling_counter == 0,   "VcuSebReq zero roll cnt");
    }

    // Rolling counter full cycle (0..15)
    for (uint8_t cnt = 0; cnt < 16; ++cnt) {
        can::VcuSebReq r{};
        r.align_enable    = 1;
        r.control_enable  = 1;
        r.roll_cnt_enable = 1;
        r.checksum_enable = 1;
        r.rolling_counter = cnt;
        r.stroke_req      = 600;

        can::Frame f{}; r.to_frame(f);
        auto unpacked = can::VcuSebReq::unpack(f.data);
        CHECK(unpacked.rolling_counter == cnt, "VcuSebReq rolling counter cycle");
    }
}

/* =================================================================== */

int main() {
    printf("=== Protocol Roundtrip Tests ===\n\n");

    printf("--- Custom protocol (big-endian) ---\n");
    test_sys_safety_sts();
    test_sys_mode_cmd();
    test_sys_throttle_sts();
    test_steer_diag();
    test_brake_diag();
    test_rt_drive_cmd();
    test_mtr_motor_fbk();
    test_rt_brake_cmd();
    test_host_light_cmd();
    test_sys_diag_rpt();
    printf("\n--- Host-side protocol ---\n");
    test_host_drive_cmd();
    test_host_brake_req();
    test_host_obstacle_dist();
    printf("\n--- SYNTREE protocol (little-endian) ---\n");
    test_vcu_ses_req();
    test_vcu_seb_req();

    printf("\n=== %s: %d failures ===\n",
           failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures;
}
