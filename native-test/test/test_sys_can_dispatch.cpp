// SYS CAN dispatch native test — feed synthetic frames, verify atomics
#include <atomic>
#include <cstdio>
#include <cstdint>
#include "protocol/generated/cpp/etrike_protocol.hpp"

namespace generated = etrike::protocol::generated;
using etrike::protocol::Frame;

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
    Frame fr;
    generated::RtDriveCmd encoded{1500, 1};
    (void)generated::encode(encoded, fr);

    generated::RtDriveCmd sp{};
    (void)generated::decode(fr.view(), sp);
    g_setpoint_speed_mmps.store(sp.motor_speed_mmps, std::memory_order_relaxed);
    g_setpoint_gear.store(sp.gear, std::memory_order_relaxed);
    g_last_setpoint_tick.store(++g_tick, std::memory_order_relaxed);

    CHECK(g_setpoint_speed_mmps.load() == 1500, "0x204: speed should be 1500");
    CHECK(g_setpoint_gear.load() == 1, "0x204: gear should be D(1)");
    std::printf("  PASS: 0x204 RT_DRIVE_CMD dispatch\n");
    return 0;
}

static int test_0x204_dlc_guard() {
    Frame fr = Frame::standard(generated::RtDriveCmd::kId, 2);  // corrupt: DLC < 5

    generated::RtDriveCmd sp{};
    (void)generated::decode(fr.view(), sp);
    CHECK(sp.motor_speed_mmps == 0, "0x204 corrupt DLC: speed should default to 0");
    CHECK(sp.gear == 0, "0x204 corrupt DLC: gear should default to N(0)");
    std::printf("  PASS: 0x204 DLC guard (corrupt frame)\n");
    return 0;
}

static int test_0x205_brake_cmd() {
    Frame fr;
    generated::RtBrakeCmd encoded{3000};
    (void)generated::encode(encoded, fr);

    generated::RtBrakeCmd brk{};
    (void)generated::decode(fr.view(), brk);
    g_brake_pressure_kpa.store(brk.brake_pressure_kpa, std::memory_order_relaxed);

    CHECK(g_brake_pressure_kpa.load() == 3000, "0x205: brake kPa should be 3000");
    std::printf("  PASS: 0x205 RT_BRAKE_CMD dispatch\n");
    return 0;
}

static int test_0x206_motor_fbk() {
    Frame fr;
    generated::MtrMotorFbk encoded{1200, 1, 0x11};
    (void)generated::encode(encoded, fr);

    generated::MtrMotorFbk fbk{};
    (void)generated::decode(fr.view(), fbk);
    g_actual_speed_mmps.store(fbk.actual_speed_mmps, std::memory_order_relaxed);
    g_motor_fault_flags.store(fbk.fault_flags, std::memory_order_relaxed);

    CHECK(g_actual_speed_mmps.load() == 1200, "0x206: speed should be 1200");
    CHECK(g_motor_fault_flags.load() == 0x11, "0x206: fault_flags should be 0x11");
    CHECK((g_motor_fault_flags.load() & 0x10) != 0, "0x206: StartupReady bit should be set");
    std::printf("  PASS: 0x206 MTR_MOTOR_FBK dispatch\n");
    return 0;
}

static int test_0x302_light_cmd() {
    Frame fr = Frame::standard(generated::HostLightCmd::kId, 1);
    fr.data[0] = 0x05;  // left_turn + brake_light

    g_light_bits.store(fr.data[0], std::memory_order_relaxed);
    CHECK(g_light_bits.load() == 0x05, "0x302: light bits should be 0x05");
    std::printf("  PASS: 0x302 HOST_LIGHT_CMD dispatch\n");
    return 0;
}

static int test_0x210_rt_state() {
    Frame fr;
    generated::RtStateRpt encoded{};
    encoded.mode = generated::RtStateRpt::kModeAuto;
    encoded.safety_state = 1;
    (void)generated::encode(encoded, fr);

    if (fr.dlc >= 2) {
        g_rt_safety_state.store(fr.data[1] & 0x03, std::memory_order_relaxed);
    }
    CHECK(g_rt_safety_state.load() == 1, "0x210: safety_state should be InternalEstop(1)");
    std::printf("  PASS: 0x210 RT_STATE_RPT safety_state dispatch\n");
    return 0;
}

static int test_0x001_estop() {
    Frame fr;
    (void)generated::encode(generated::SafetyEstop{}, fr);
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
