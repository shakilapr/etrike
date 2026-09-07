// WP2 MTR — DiagnosticManager instrumentation integration test.
// Exercises the REAL MTR firmware classes (MotorManager, CanDriver) against the
// stub HAL, verifying that existing Phase A fault detectors fire the correct
// Phase B DiagId raise() calls (report-only, no reaction change) and that the
// 0x631 event-report wire encoding matches the generated codec (mirrors main.cpp).
#include <cstdint>
#include <cstdio>
#include <cstring>

// 1. Include STM32 HAL stub before subsystem headers
#include "stub/stm32g4xx_hal.h"

// Define dummy global FDCAN handle needed by CanDriver extern "C"
extern "C" {
    FDCAN_HandleTypeDef hfdcan1;
}

// 2. Include actual production MTR headers + Phase B reporting + protocol
#include "mtr-stm32/src/config.h"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"
#include "mtr-stm32/src/can_driver.h"
#include "shared/diagnostics.h"
#include "protocol/compat/can.hpp"
#include "shared_config.h"

namespace {

int g_tests_run = 0;
int g_tests_failed = 0;

#define ASSERT_TRUE(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        std::printf("  FAIL [%s:%d]: Condition failed: %s\n", __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(val, target) do { \
    g_tests_run++; \
    if (!((val) == (target))) { \
        std::printf("  FAIL [%s:%d]: %s (%d) != %s (%d)\n", __FILE__, __LINE__, \
                    #val, (int)(val), #target, (int)(target)); \
        g_tests_failed++; \
    } \
} while(0)

using namespace mtr;
using namespace etrike::diagnostics;

// Build a SYS_SAFETY_STS (0x011) frame. If bad_crc=true, encode with a wrong E2E CRC
// so the recompute check fails (decoder still accepts the frame structurally).
can::Frame make_safety_sts(bool estop, bool bad_crc, int counter) {
    can::gen::SysSafetySts msg{};
    msg.estop_active = estop;
    msg.heartbeat_ok = true;
    msg.light_left = false;
    msg.light_brake = false;
    msg.light_head = false;
    msg.rolling_counter = static_cast<std::uint8_t>(counter);
    can::Frame fr{};
    can::gen::encode_sys_safety_sts(msg, fr);
    if (!bad_crc) {
        msg.e2e_crc = ::etrike::protocol::e2e::sys_safety_sts_crc(fr.data.data());
        can::gen::encode_sys_safety_sts(msg, fr);
    }
    return fr;
}

can::Frame make_rt_drive_cmd() {
    can::gen::RtDriveCmd cmd{};
    cmd.motor_speed_mmps = 1000;
    cmd.gear = 1;
    can::Frame fr{};
    can::gen::encode_rt_drive_cmd(cmd, fr);
    return fr;
}

can::Frame make_sys_mode_cmd(int counter) {
    can::gen::SysModeCmd cmd{};
    cmd.mode = 1;  // Auto
    cmd.rolling_counter = static_cast<std::uint8_t>(counter);
    can::Frame fr{};
    can::gen::encode_sys_mode_cmd(cmd, fr);
    return fr;
}

can::Frame make_sys_pwr_cmd(bool on, int counter) {
    can::gen::SysPwrCmd cmd{};
    cmd.power_state = on ? 1 : 0;
    cmd.rolling_counter = static_cast<std::uint8_t>(counter);
    can::Frame fr{};
    can::gen::encode_sys_pwr_cmd(cmd, fr);
    return fr;
}

// --- MotorManager Phase B reporting ---------------------------------------------
void test_motor_manager_diagnostics() {
    std::printf("test_motor_manager_diagnostics...\n");

    RelayController relays;
    DacController dac;
    MotorManager motor(relays, dac);
    DiagnosticManager diag;
    motor.set_diag(diag);

    // 1) CRC error on SYS_SAFETY_STS raises MtrSysSafetyCrcError, report-only.
    motor.handle_frame(make_safety_sts(false, /*bad_crc=*/true, 0), 1000);
    ASSERT_TRUE(diag.state_of(DiagId::MtrSysSafetyCrcError) == DiagState::Active);
    ASSERT_EQ(diag.occurrence_of(DiagId::MtrSysSafetyCrcError), 1);
    ASSERT_TRUE(diag.state_of(DiagId::MtrSysSafetyCounterStale) == DiagState::Cleared);

    // 2) Two advancing valid SYS_SAFETY_STS frames (counter 0 -> 1) reacquire authority.
    motor.handle_frame(make_safety_sts(false, false, 0), 1010);
    motor.handle_frame(make_safety_sts(false, false, 1), 1011);
    // CRC error remains latched (report-only, no recovery path on its own).
    ASSERT_TRUE(diag.state_of(DiagId::MtrSysSafetyCrcError) == DiagState::Active);

    // 3) Unauthorized RT_DRIVE_CMD (mode authority lost initially) raises MtrCmdStreamUnauthorised.
    motor.handle_frame(make_rt_drive_cmd(), 1020);
    ASSERT_TRUE(diag.state_of(DiagId::MtrCmdStreamUnauthorised) == DiagState::Active);

    // 4) Safety freshness timeout in tick() raises MtrSysSafetyStsTimeout with snapshot.
    motor.tick(1011 + 700 + 1);  // exceeds kSafetyFreshMs (700) since last valid safety frame
    ASSERT_TRUE(diag.state_of(DiagId::MtrSysSafetyStsTimeout) == DiagState::Active);
    ASSERT_TRUE(diag.snapshot_supplied_of(DiagId::MtrSysSafetyStsTimeout));

    // 5) Re-assert is idempotent: no occurrence increment / no second pending storm.
    DiagReport r{};
    ASSERT_TRUE(diag.pop_pending_report(r));  // drain first pending
    motor.handle_frame(make_safety_sts(false, true, 2), 2000);  // same CRC error again
    ASSERT_EQ(diag.occurrence_of(DiagId::MtrSysSafetyCrcError), 1);

    // 6) Rearm sequence violation: ESTOP -> authorized clear invalidates power
    //    authority -> a 0x113 ON arrives (after reacquisition) without the required
    //    OFF edge having been observed (rearm_required_ && !rearm_off_seen_).
    {
        DiagnosticManager diag2;
        MotorManager m2(relays, dac);
        m2.set_diag(diag2);
        m2.handle_frame(make_sys_mode_cmd(0), 10);      // mode baseline
        m2.handle_frame(make_sys_mode_cmd(1), 11);      // mode valid
        m2.handle_frame(make_sys_pwr_cmd(true, 0), 12); // power baseline
        m2.handle_frame(make_sys_pwr_cmd(true, 1), 13); // power valid
        m2.handle_frame(make_safety_sts(false, false, 0), 14);
        m2.handle_frame(make_safety_sts(false, false, 1), 15); // safety valid (reacquired)
        m2.handle_frame(can::Frame{can::kIdSafetyEstop, false, 0}, 16); // hard ESTOP (0x001)
        m2.handle_frame(make_safety_sts(false, false, 2), 17); // clear seq frame 1
        m2.handle_frame(make_safety_sts(false, false, 3), 18); // clear seq frame 2 -> authorized_clear
        // Reacquire power WITHOUT an OFF edge: first ON is baseline, second ON is
        // the violating edge (rearm_required_ still true, rearm_off_seen_ still false).
        m2.handle_frame(make_sys_pwr_cmd(true, 2), 19); // post-clear baseline
        m2.handle_frame(make_sys_pwr_cmd(true, 3), 20); // valid -> REARM violation
        ASSERT_TRUE(diag2.state_of(DiagId::MtrRearmSequenceViolation) == DiagState::Active);
    }
}

// --- CanDriver Phase B reporting (bus-off) --------------------------------------
void test_can_driver_bus_off() {
    std::printf("test_can_driver_bus_off...\n");

    fdcan_mock::reset();
    DiagnosticManager diag;
    CanDriver can;
    can.set_diag(diag);
    ASSERT_TRUE(can.init());

    // Simulate a Bus-Off condition: PSR.BO set, ECR = TEC=0x42, REC=0x07.
    g_fdcan1_regs.PSR = FDCAN_PSR_BO;
    g_fdcan1_regs.ECR = (0x42u << 8) | 0x07u;
    can.service_recovery();

    ASSERT_TRUE(diag.state_of(DiagId::MtrFdcanBusOff) == DiagState::Active);
    DiagReport r{};
    ASSERT_TRUE(diag.pop_pending_report(r));
    ASSERT_EQ(static_cast<int>(r.id), static_cast<int>(DiagId::MtrFdcanBusOff));
    ASSERT_EQ(r.snapshot_data, static_cast<std::uint16_t>((0x42u << 8) | 0x07u));
}

// --- 0x631 wire encoding (mirrors main.cpp drain) ------------------------------
void test_mtr_diag_event_rpt_wire() {
    std::printf("test_mtr_diag_event_rpt_wire...\n");

    // Build a live report via the MotorManager, then encode exactly as main.cpp does.
    RelayController relays;
    DacController dac;
    DiagnosticManager diag;
    MotorManager motor(relays, dac);
    motor.set_diag(diag);
    motor.handle_frame(make_safety_sts(false, true, 0), 1000);  // MtrSysSafetyCrcError

    DiagReport r{};
    ASSERT_TRUE(diag.pop_pending_report(r));

    can::gen::MtrDiagEventRpt out{};
    out.diag_id = static_cast<std::uint16_t>(r.id);
    out.state = static_cast<std::uint8_t>(r.state);
    out.occurrence_count = r.occurrence_count;
    out.report_counter = r.report_counter;
    out.flags = r.flags;
    out.snapshot_data = r.snapshot_data;

    can::Frame enc{};
    can::gen::encode_mtr_diag_event_rpt(out, enc);
    ASSERT_EQ(enc.id, 0x631u);
    ASSERT_EQ(enc.dlc, 8);

    // Round-trip decode and verify field mapping.
    can::gen::MtrDiagEventRpt back{};
    ASSERT_EQ(can::gen::decode_mtr_diag_event_rpt(enc.view(), back), can::gen::CodecStatus::Ok);
    ASSERT_EQ(back.diag_id, out.diag_id);
    ASSERT_EQ(back.state, out.state);
    ASSERT_EQ(back.occurrence_count, out.occurrence_count);
    ASSERT_EQ(back.report_counter, out.report_counter);
    ASSERT_EQ(back.flags, out.flags);
    ASSERT_EQ(back.snapshot_data, out.snapshot_data);

    // Confirm the real CanDriver::send path transports it on 0x631.
    fdcan_mock::reset();
    CanDriver can;
    ASSERT_TRUE(can.init());
    ASSERT_TRUE(can.send(enc));
    ASSERT_FALSE(fdcan_mock::g_tx_msgs.empty());
    ASSERT_EQ(static_cast<int>(fdcan_mock::g_tx_msgs.back().id), 0x631);
}

} // namespace

int main() {
    std::printf("========================================\n");
    std::printf("  MTR-STM32 WP2 Diagnostic Reporting\n");
    std::printf("========================================\n");

    test_motor_manager_diagnostics();
    test_can_driver_bus_off();
    test_mtr_diag_event_rpt_wire();

    std::printf("----------------------------------------\n");
    std::printf("Tests Run: %d | Failures: %d\n", g_tests_run, g_tests_failed);
    if (g_tests_failed == 0) {
        std::printf("ALL MTR-STM32 WP2 DIAGNOSTIC TESTS PASSED!\n");
        return 0;
    }
    std::printf("SOME TESTS FAILED!\n");
    return 1;
}
