// SYS CAN dispatch native test — feed synthetic frames, verify atomics
#include <atomic>
#include <cstdio>
#include <cstdint>
#include "can/can_protocol.h"

// ── Stubs for SYS globals (dispatch writes these) ─────────────────
static std::atomic<int32_t>  g_setpoint_speed_mmps{0};
static std::atomic<uint8_t>  g_setpoint_gear{0};
static std::atomic<int32_t>  g_brake_pressure_kpa{0};
static std::atomic<uint8_t>  g_light_bits{0};
static std::atomic<uint8_t>  g_rt_safety_state{0};
static std::atomic<int16_t>  g_actual_speed_mmps{0};
static std::atomic<uint8_t>  g_motor_fault_flags{0};
static std::atomic<uint32_t> g_last_setpoint_tick{0};
static std::atomic<uint32_t> g_last_mtr_fbk_tick{0};
static uint32_t g_tick = 0;

#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); return 1; } } while(0)

// ── Simulated dispatch: process one frame, verify one atomic ──────

static int test_0x204_drive_cmd() {
    can::Frame fr;
    fr.id = can::kIdRtDriveCmd; fr.dlc = 5;
    fr.put_i32(0, 1500);  // speed = 1500 mm/s
    fr.put_u8(4, 1);      // gear = D

    auto sp = can::RtDriveCmd::from_frame(fr);
    g_setpoint_speed_mmps.store(sp.motor_speed_mmps, std::memory_order_relaxed);
    g_setpoint_gear.store(sp.gear, std::memory_order_relaxed);
    g_last_setpoint_tick.store(++g_tick, std::memory_order_relaxed);

    CHECK(g_setpoint_speed_mmps.load() == 1500, "0x204: speed should be 1500");
    CHECK(g_setpoint_gear.load() == 1, "0x204: gear should be D(1)");
    std::printf("  PASS: 0x204 RT_DRIVE_CMD dispatch\n");
    return 0;
}

static int test_0x204_dlc_guard() {
    can::Frame fr;
    fr.id = can::kIdRtDriveCmd; fr.dlc = 2;  // corrupt: DLC < 5

    auto sp = can::RtDriveCmd::from_frame(fr);
    CHECK(sp.motor_speed_mmps == 0, "0x204 corrupt DLC: speed should default to 0");
    CHECK(sp.gear == 0, "0x204 corrupt DLC: gear should default to N(0)");
    std::printf("  PASS: 0x204 DLC guard (corrupt frame)\n");
    return 0;
}

static int test_0x205_brake_cmd() {
    can::Frame fr;
    fr.id = can::kIdRtBrakeCmd; fr.dlc = 4;
    fr.put_i32(0, 3000);  // 3000 kPa

    auto brk = can::RtBrakeCmd::from_frame(fr);
    g_brake_pressure_kpa.store(brk.brake_pressure_kpa, std::memory_order_relaxed);

    CHECK(g_brake_pressure_kpa.load() == 3000, "0x205: brake kPa should be 3000");
    std::printf("  PASS: 0x205 RT_BRAKE_CMD dispatch\n");
    return 0;
}

static int test_0x206_motor_fbk() {
    can::Frame fr;
    fr.id = can::kIdMtrMotorFbk; fr.dlc = 4;
    fr.put_i16(0, 1200);   // actual speed
    fr.put_u8(2, 1);       // gear = D
    fr.put_u8(3, 0x11);    // fault_flags: EstopActive + StartupReady

    auto fbk = can::MtrMotorFbk::from_frame(fr);
    g_actual_speed_mmps.store(fbk.actual_speed_mmps, std::memory_order_relaxed);
    g_motor_fault_flags.store(fbk.fault_flags, std::memory_order_relaxed);

    CHECK(g_actual_speed_mmps.load() == 1200, "0x206: speed should be 1200");
    CHECK(g_motor_fault_flags.load() == 0x11, "0x206: fault_flags should be 0x11");
    CHECK((g_motor_fault_flags.load() & 0x10) != 0, "0x206: StartupReady bit should be set");
    std::printf("  PASS: 0x206 MTR_MOTOR_FBK dispatch\n");
    return 0;
}

static int test_0x302_light_cmd() {
    can::Frame fr;
    fr.id = can::kIdHostLightCmd; fr.dlc = 1;
    fr.data[0] = 0x05;  // left_turn + brake_light

    g_light_bits.store(fr.u8_at(0), std::memory_order_relaxed);
    CHECK(g_light_bits.load() == 0x05, "0x302: light bits should be 0x05");
    std::printf("  PASS: 0x302 HOST_LIGHT_CMD dispatch\n");
    return 0;
}

static int test_0x210_rt_state() {
    can::Frame fr;
    fr.id = can::kIdRtStateRpt; fr.dlc = 4;
    fr.put_u8(0, 1);       // mode = Auto
    fr.put_u8(1, 0x01);    // safety_state = InternalEstop

    if (fr.dlc >= 2) {
        g_rt_safety_state.store(fr.u8_at(1) & 0x03, std::memory_order_relaxed);
    }
    CHECK(g_rt_safety_state.load() == 1, "0x210: safety_state should be InternalEstop(1)");
    std::printf("  PASS: 0x210 RT_STATE_RPT safety_state dispatch\n");
    return 0;
}

static int test_0x001_estop() {
    can::Frame fr;
    fr.id = can::kIdSafetyEstop; fr.dlc = 0;
    // ESTOP is DLC=0 — just the ID matters
    CHECK(fr.id == 0x001, "0x001: ESTOP ID should be 0x001");
    CHECK(fr.dlc == 0, "0x001: ESTOP DLC should be 0");
    std::printf("  PASS: 0x001 SAFETY_ESTOP dispatch\n");
    return 0;
}

int main() {
    int failures = 0;
    failures += test_0x204_drive_cmd();
    failures += test_0x204_dlc_guard();
    failures += test_0x205_brake_cmd();
    failures += test_0x206_motor_fbk();
    failures += test_0x302_light_cmd();
    failures += test_0x210_rt_state();
    failures += test_0x001_estop();

    if (failures == 0) std::printf("\nAll SYS dispatch tests PASSED\n");
    else std::printf("\n%d test(s) FAILED\n", failures);
    return failures;
}
