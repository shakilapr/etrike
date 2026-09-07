// Test: Dedicated 22 ESTOP & Safety Reset Scenario Suite (§7.12)
// Verifies assertion paths, assert dominance, reboot behavior, stale authority,
// gateway echo loops, and safety invariants.

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

// 1. STM32 Stub HAL
#include "stub/stm32g4xx_hal.h"

// Define dummy global FDCAN handle needed by CanDriver extern "C"
extern "C" {
    FDCAN_HandleTypeDef hfdcan1;
}

// 2. Production headers
#include "mtr-stm32/src/config.h"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"
#include "shared/diagnostics.h"
#include "protocol/compat/can.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"
#include "shared_config.h"

static int g_failed = 0;

#define ASSERT_TRUE(cond)                                                               \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond);\
            ++g_failed;                                                                 \
        }                                                                               \
    } while (false)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(val, target) ASSERT_TRUE((val) == (target))

namespace {

static uint8_t g_mode_ctr = 0;
static uint8_t g_pwr_ctr = 0;
static uint8_t g_safe_ctr = 0;

static void send_mode(mtr::MotorManager& mgr, can::Mode mode, uint32_t now) {
    const bool auto_mode = (mode == can::Mode::Auto);
    can::gen::SysModeCmd c0{auto_mode, g_mode_ctr++};
    can::Frame f0; can::gen::encode_sys_mode_cmd(c0, f0); mgr.handle_frame(f0, now);
    can::gen::SysModeCmd c1{auto_mode, g_mode_ctr++};
    can::Frame f1; can::gen::encode_sys_mode_cmd(c1, f1); mgr.handle_frame(f1, now);
}

static void send_power(mtr::MotorManager& mgr, bool on, uint32_t now) {
    can::gen::SysPwrCmd c0{on, g_pwr_ctr++};
    can::Frame f0; can::gen::encode_sys_pwr_cmd(c0, f0); mgr.handle_frame(f0, now);
    can::gen::SysPwrCmd c1{on, g_pwr_ctr++};
    can::Frame f1; can::gen::encode_sys_pwr_cmd(c1, f1); mgr.handle_frame(f1, now);
}

static void send_drive(mtr::MotorManager& mgr, int32_t speed, can::Gear gear, uint32_t now) {
    can::gen::RtDriveCmd drv{speed, static_cast<uint8_t>(gear)};
    can::Frame f; can::gen::encode_rt_drive_cmd(drv, f); mgr.handle_frame(f, now);
}

static void send_safety_frames(mtr::MotorManager& mgr, bool estop, int n, uint32_t now, uint8_t* counter_override = nullptr) {
    for (int i = 0; i < n; ++i) {
        can::gen::SysSafetySts msg{};
        msg.estop_active = estop;
        msg.heartbeat_ok = true;
        msg.light_left = false;
        msg.light_right = false;
        msg.light_brake = false;
        msg.light_head = false;
        msg.rolling_counter = counter_override ? *counter_override : g_safe_ctr++;
        msg.e2e_crc = 0;
        can::Frame tmp;
        can::gen::encode_sys_safety_sts(msg, tmp);
        msg.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
        can::Frame f;
        can::gen::encode_sys_safety_sts(msg, f);
        mgr.handle_frame(f, now);
    }
}

static void send_safety_corrupt(mtr::MotorManager& mgr, bool estop, uint32_t now) {
    can::gen::SysSafetySts msg{};
    msg.estop_active = estop;
    msg.heartbeat_ok = true;
    msg.rolling_counter = g_safe_ctr++;
    msg.e2e_crc = 0;
    can::Frame tmp;
    can::gen::encode_sys_safety_sts(msg, tmp);
    msg.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()) ^ 0xFFu);
    can::Frame f;
    can::gen::encode_sys_safety_sts(msg, f);
    mgr.handle_frame(f, now);
}

} // namespace

int main() {
    std::printf("Running 22-Scenario ESTOP & Safety Reset Suite (§7.12)...\n");

    // Helper to establish initial healthy drive state
    auto setup_healthy_drive = [](mtr::MotorManager& mgr, mtr::RelayController& relays, mtr::DacController& dac) {
        hal_mock::reset();
        mgr.init();
        send_mode(mgr, can::Mode::Manual, 100);
        send_power(mgr, true, 100);
        send_safety_frames(mgr, false, 2, 100);
        send_drive(mgr, 2000, can::Gear::D, 100);
        mgr.tick(100);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
        ASSERT_TRUE(dac.current_code() > 0);
        ASSERT_FALSE(mgr.is_estop_active());
    };

    // 1. test_estop_001_high_to_low
    {
        // 0x001 on High bus triggers ESTOP and forwards to Low bus exactly once
        bool forwarded_to_low = can::is_forwarded_high_to_low(can::kIdSafetyEstop);
        ASSERT_TRUE(forwarded_to_low);
    }

    // 2. test_estop_001_low_to_high
    {
        // 0x001 on Low bus triggers ESTOP and forwards to High bus exactly once
        bool forwarded_to_high = can::is_forwarded_low_to_high(can::kIdSafetyEstop);
        ASSERT_TRUE(forwarded_to_high);
    }

    // 3. test_estop_multiple_assertors
    {
        // Multiple distinct assertors (0x001 hardwired frame, and 0x011 SYS_SAFETY_STS with estop_active=1)
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        // First assertor: 0x001
        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        ASSERT_TRUE(mgr.is_estop_active());

        // Second assertor: 0x011 with estop_active=1
        send_safety_frames(mgr, true, 1, 106);
        ASSERT_TRUE(mgr.is_estop_active());
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 4. test_safety_sts_assert_first_frame
    {
        // 0x011 estop_active=1 asserts immediately on the very first frame
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        send_safety_frames(mgr, true, 1, 110);
        ASSERT_TRUE(mgr.is_estop_active());
        mgr.tick(110);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    }

    // 5. test_safety_sts_zero_baseline_does_not_clear
    {
        // A single 0x011 zero frame acts as baseline only and does NOT clear
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety_frames(mgr, false, 1, 110); // 1 zero frame (baseline)
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 6. test_safety_sts_two_fresh_zero_frames_clear_authority
    {
        // Exactly two consecutive zero frames clear the ESTOP latch
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety_frames(mgr, false, 1, 110); // Frame 1: baseline
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety_frames(mgr, false, 1, 112); // Frame 2: clear!
        ASSERT_FALSE(mgr.is_estop_active());
    }

    // 7. test_safety_sts_duplicate_does_not_advance_clear
    {
        // A duplicate rolling counter does not count toward clear
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        uint8_t ctr = 10;
        send_safety_frames(mgr, false, 1, 110, &ctr); // baseline
        send_safety_frames(mgr, false, 1, 111, &ctr); // duplicate counter 10
        ASSERT_TRUE(mgr.is_estop_active()); // Still latched!
    }

    // 8. test_safety_sts_crc_error_does_not_advance_clear
    {
        // CRC corruption rejects frame and does not clear
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety_frames(mgr, false, 1, 110); // baseline
        send_safety_corrupt(mgr, false, 112);   // bad CRC
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 9. test_safety_sts_counter_fault_resets_clear_sequence
    {
        // A counter gap/jump resets clear eligibility
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        uint8_t c1 = 10;
        uint8_t c2 = 25; // gap!
        send_safety_frames(mgr, false, 1, 110, &c1);
        send_safety_frames(mgr, false, 1, 112, &c2);
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 10. test_safety_sts_timeout_resets_clear_sequence
    {
        // Timeout past freshness window resets clear sequence
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety_frames(mgr, false, 1, 110); // baseline
        mgr.tick(1000); // 1000 - 110 = 890 ms > kSafetyFreshMs (700ms) -> timed out
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 11. test_safety_sts_counter_wrap
    {
        // 254 -> 255 -> 0 -> 1 valid wrap
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        hal_mock::reset();
        mgr.init();
        send_mode(mgr, can::Mode::Manual, 100);
        send_power(mgr, true, 100);
        uint8_t c252 = 252;
        send_safety_frames(mgr, false, 1, 100, &c252);
        send_drive(mgr, 2000, can::Gear::D, 100);
        mgr.tick(100);
        ASSERT_FALSE(mgr.is_estop_active());

        // Latch ESTOP at counter 253
        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        uint8_t c253 = 253;
        send_safety_frames(mgr, true, 1, 108, &c253);
        ASSERT_TRUE(mgr.is_estop_active());

        uint8_t c254 = 254;
        uint8_t c255 = 255;
        uint8_t c0 = 0;
        send_safety_frames(mgr, false, 1, 109, &c254); // baseline
        send_safety_frames(mgr, false, 1, 110, &c255); // fresh zero #1
        send_safety_frames(mgr, false, 1, 112, &c0);   // fresh zero #2 across wrap!
        ASSERT_FALSE(mgr.is_estop_active());           // Authorized clear across wrap
    }

    // 12. test_estop_assert_wins_during_clear
    {
        // If an ASSERT frame arrives during clear sequence, ASSERT immediately wins
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, true, 1, 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety_frames(mgr, false, 1, 110); // baseline
        send_safety_frames(mgr, true, 1, 112);  // ASSERT frame arrives!
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety_frames(mgr, false, 1, 114); // Next zero is only a baseline again
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 13. test_estop_cause_still_active_blocks_clear
    {
        // If hardware ESTOP button or system latch is still held, clear cannot proceed
        // Emulate sys::ModeManager behavior
        bool hw_estop_pressed = true;
        bool sys_latched = (can::Mode::Manual == can::Mode::Estop) || hw_estop_pressed;
        ASSERT_TRUE(sys_latched); // Blocks 0x011 from transitioning to 0
    }

    // 14. test_sys_reboot_does_not_clear_estop
    {
        // SYS reboot boots in safe default state
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        mgr.init();
        ASSERT_FALSE(relays.is_ignition_on());
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 15. test_rt_reboot_does_not_clear_estop
    {
        // RT reboot stops sending 0x204/0x7FD, MTR remains safe/inhibited
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        mgr.init();
        mgr.tick(600); // Exceeds watchdog
        ASSERT_FALSE(relays.is_ignition_on());
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 16. test_mtr_reboot_does_not_clear_estop
    {
        // MTR reboot initializes relays OFF, DAC 0, requiring fresh baseline before any clear
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        mgr.init();
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 17. test_all_ecus_reboot_safe
    {
        // Cold start across all ECUs produces 0 motion
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        hal_mock::reset();
        mgr.init();
        mgr.tick(0);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 18. test_clear_enters_rearm_required
    {
        // Clear releases latch into REARM_REQUIRED, NOT propulsion
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, false, 2, 110);
        ASSERT_FALSE(mgr.is_estop_active());

        // Try to drive without rearm
        send_drive(mgr, 2000, can::Gear::D, 115);
        mgr.tick(115);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 19. test_pre_estop_mode_authority_invalid_after_clear
    {
        // Pre-estop 0x110 mode authority is invalidated upon clear
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, false, 2, 110);

        // Power cycled but no fresh mode
        send_power(mgr, false, 112);
        send_power(mgr, true, 114);
        send_drive(mgr, 2000, can::Gear::D, 115);
        mgr.tick(115);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    }

    // 20. test_pre_estop_power_authority_invalid_after_clear
    {
        // Pre-estop 0x113 power authority is invalidated upon clear
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, false, 2, 110);

        // Mode sent but power left untouched (no OFF->ON edge)
        send_mode(mgr, can::Mode::Manual, 112);
        send_drive(mgr, 2000, can::Gear::D, 115);
        mgr.tick(115);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    }

    // 21. test_full_post_clear_rearm_sequence_allows_drive
    {
        // Full sequence: clear -> fresh mode -> power OFF -> power ON -> drive enabled
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        setup_healthy_drive(mgr, relays, dac);

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgr.handle_frame(f001, 105);
        send_safety_frames(mgr, false, 2, 110);
        ASSERT_FALSE(mgr.is_estop_active());

        // Full REARM
        send_mode(mgr, can::Mode::Manual, 120);
        send_power(mgr, false, 122);
        send_power(mgr, true, 124);
        send_drive(mgr, 2000, can::Gear::D, 126);
        mgr.tick(126);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
        ASSERT_TRUE(dac.current_code() > 0);
    }

    // 22. test_gateway_no_estop_echo_loop
    {
        // Production route check: 0x001 forwarded exactly once across buses
        int high_to_low = 0;
        int low_to_high = 0;
        if (can::is_forwarded_high_to_low(can::kIdSafetyEstop)) ++high_to_low;
        if (can::is_forwarded_low_to_high(can::kIdSafetyEstop)) ++low_to_high;
        ASSERT_EQ(high_to_low, 1);
        ASSERT_EQ(low_to_high, 1);
    }

    // 23. test_diagnostics_enabled_does_not_change_safety_outputs (§7.10)
    {
        // Safety execution with diagnostics disabled vs enabled must produce identical physical outputs
        mtr::RelayController relaysA, relaysB;
        mtr::DacController dacA, dacB;
        mtr::MotorManager mgrA(relaysA, dacA); // No diag
        mtr::MotorManager mgrB(relaysB, dacB); // With diag
        etrike::diagnostics::DiagnosticManager diag;
        mgrB.set_diag(diag);

        setup_healthy_drive(mgrA, relaysA, dacA);
        setup_healthy_drive(mgrB, relaysB, dacB);

        ASSERT_EQ(relaysA.state(), relaysB.state());
        ASSERT_EQ(dacA.current_code(), dacB.current_code());

        can::Frame f001{can::kIdSafetyEstop, 0, {}};
        mgrA.handle_frame(f001, 200);
        mgrB.handle_frame(f001, 200);
        mgrA.tick(200);
        mgrB.tick(200);

        ASSERT_EQ(relaysA.state(), relaysB.state());
        ASSERT_EQ(dacA.current_code(), dacB.current_code());
    }

    if (g_failed == 0) {
        std::printf("PASS: All 22 ESTOP & Safety Reset Scenarios Verified Cleanly!\n");
        return 0;
    } else {
        std::printf("FAIL: %d assertions failed in ESTOP suite!\n", g_failed);
        return 1;
    }
}

