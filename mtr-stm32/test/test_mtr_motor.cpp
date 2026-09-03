#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "shared_config.h"
#include "mtr-stm32/src/config.h"

// Simulated Relay Hardware State for Host Testing
struct MockRelayState {
    bool reverse{false};
    bool drive{false};
    bool ignition{false};

    void apply(uint8_t state) {
        switch (state) {
        case 2: // Drive
            drive = true;
            reverse = false;
            ignition = true;
            break;
        case 3: // Reverse
            drive = false;
            reverse = true;
            ignition = true;
            break;
        case 1: // Park
            drive = false;
            reverse = false;
            ignition = true;
            break;
        case 0: // Off
        default:
            drive = false;
            reverse = false;
            ignition = false;
            break;
        }
    }
};

// Pure Logic Test Engine for MTR
class MockMotorManager {
public:
    void init() {
        estop_active = false;
        target_speed_mmps = 0;
        target_gear = can::Gear::N;
        relays.apply(0);
        dac_code = 0;
    }

    void handle_drive_cmd(int32_t speed_mmps, can::Gear gear) {
        if (estop_active) return;
        target_speed_mmps = speed_mmps;
        target_gear = gear;
    }

    void trigger_estop() {
        estop_active = true;
        target_speed_mmps = 0;
        target_gear = can::Gear::N;
        relays.apply(0);
        dac_code = 0;
    }

    void tick() {
        if (estop_active) {
            relays.apply(0);
            dac_code = 0;
            return;
        }

        switch (target_gear) {
        case can::Gear::D:
        case can::Gear::S:
            relays.apply(2);
            break;
        case can::Gear::R:
            relays.apply(3);
            break;
        case can::Gear::N:
        default:
            relays.apply(1);
            break;
        }

        bool fwd = (target_gear == can::Gear::D || target_gear == can::Gear::S);
        bool rev = (target_gear == can::Gear::R);
        int32_t eff_speed = fwd ? target_speed_mmps : (rev ? -target_speed_mmps : 0);
        if (eff_speed <= 0 && rev && target_speed_mmps > 0) {
            eff_speed = target_speed_mmps;
        }

        if (target_gear == can::Gear::N || eff_speed <= 0) {
            dac_code = 0;
        } else {
            int32_t max_speed = fwd ? mtr::kMaxForwardSpeedMmps : mtr::kMaxReverseSpeedMmps;
            float norm = static_cast<float>(eff_speed) / static_cast<float>(max_speed);
            norm = std::clamp(norm, 0.0f, 1.0f);
            float code_f = static_cast<float>(mtr::kDacMinCode) + norm * static_cast<float>(mtr::kDacMaxCode - mtr::kDacMinCode);
            dac_code = static_cast<uint16_t>(code_f);
        }
    }

    can::Frame build_feedback() const {
        can::gen::MtrMotorFbk fbk{};
        fbk.actual_speed_mmps = static_cast<int16_t>(target_speed_mmps);
        fbk.gear_state = (relays.drive) ? 1 : ((relays.reverse) ? 3 : 0);
        uint8_t flags = 0;
        if (estop_active) {
            flags |= shared::kMtrFaultEstopActive;
        }
        flags |= shared::kMtrFaultStartupReady;
        fbk.fault_flags = flags;
        can::Frame fr;
        can::gen::encode_mtr_motor_fbk(fbk, fr);
        return fr;
    }

    can::Frame build_throttle_sts() const {
        can::gen::SysThrottleSts sts{};
        sts.speed_mmps = static_cast<int16_t>(target_speed_mmps);
        can::Frame fr;
        can::gen::encode_sys_throttle_sts(sts, fr);
        return fr;
    }

    bool estop_active{false};
    int32_t target_speed_mmps{0};
    can::Gear target_gear{can::Gear::N};
    MockRelayState relays;
    uint16_t dac_code{0};
};

namespace {

int g_tests_run = 0;
int g_tests_failed = 0;

#define ASSERT_TRUE(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        std::printf("FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_EQ(val, target) do { \
    g_tests_run++; \
    if ((val) != (target)) { \
        std::printf("FAIL [%s:%d]: val %d != target %d\n", __FILE__, __LINE__, static_cast<int>(val), static_cast<int>(target)); \
        g_tests_failed++; \
    } \
} while(0)

void test_relay_mutual_exclusion() {
    MockMotorManager mgr;
    mgr.init();

    // 1. Neutral/Park: Ignition ON, Drive OFF, Reverse OFF
    mgr.handle_drive_cmd(0, can::Gear::N);
    mgr.tick();
    ASSERT_TRUE(mgr.relays.ignition);
    ASSERT_TRUE(!mgr.relays.drive);
    ASSERT_TRUE(!mgr.relays.reverse);

    // 2. Drive: Ignition ON, Drive ON, Reverse OFF
    mgr.handle_drive_cmd(1500, can::Gear::D);
    mgr.tick();
    ASSERT_TRUE(mgr.relays.ignition);
    ASSERT_TRUE(mgr.relays.drive);
    ASSERT_TRUE(!mgr.relays.reverse);

    // 3. Reverse: Ignition ON, Drive OFF, Reverse ON
    mgr.handle_drive_cmd(200, can::Gear::R);
    mgr.tick();
    ASSERT_TRUE(mgr.relays.ignition);
    ASSERT_TRUE(!mgr.relays.drive);
    ASSERT_TRUE(mgr.relays.reverse);

    // Strict Mutual Exclusion: Drive and Reverse must never both be true
    ASSERT_TRUE(!(mgr.relays.drive && mgr.relays.reverse));
}

void test_dac_throttle_curve_and_clamps() {
    MockMotorManager mgr;
    mgr.init();

    // Zero speed in Neutral -> 0 V (Code 0)
    mgr.handle_drive_cmd(0, can::Gear::N);
    mgr.tick();
    ASSERT_EQ(mgr.dac_code, 0);

    // Zero speed in Drive -> 0 V (Code 0)
    mgr.handle_drive_cmd(0, can::Gear::D);
    mgr.tick();
    ASSERT_EQ(mgr.dac_code, 0);

    // Minimum active speed in Drive -> clamps to 655 (0.8 V)
    mgr.handle_drive_cmd(1, can::Gear::D);
    mgr.tick();
    ASSERT_TRUE(mgr.dac_code >= mtr::kDacMinCode);

    // 50% Speed (1500 mm/s) -> Midpoint ~ 1310
    mgr.handle_drive_cmd(1500, can::Gear::D);
    mgr.tick();
    ASSERT_TRUE(mgr.dac_code > 1250 && mgr.dac_code < 1350);

    // 100% Speed (3000 mm/s) -> 1966 (2.4 V ceiling)
    mgr.handle_drive_cmd(3000, can::Gear::D);
    mgr.tick();
    ASSERT_EQ(mgr.dac_code, mtr::kDacMaxCode);

    // Over-speed (4000 mm/s) -> Still clamped at 1966
    mgr.handle_drive_cmd(4000, can::Gear::D);
    mgr.tick();
    ASSERT_EQ(mgr.dac_code, mtr::kDacMaxCode);

    // Reverse test with negative speed (-250 mm/s -> 50% reverse speed -> Midpoint)
    mgr.handle_drive_cmd(-250, can::Gear::R);
    mgr.tick();
    ASSERT_TRUE(mgr.dac_code > 1250 && mgr.dac_code < 1350);

    // Reverse test with max negative speed (-500 mm/s -> 100% reverse speed -> 1966)
    mgr.handle_drive_cmd(-500, can::Gear::R);
    mgr.tick();
    ASSERT_EQ(mgr.dac_code, mtr::kDacMaxCode);

    // Reverse test with positive magnitude (500 mm/s -> 100% reverse speed -> 1966)
    mgr.handle_drive_cmd(500, can::Gear::R);
    mgr.tick();
    ASSERT_EQ(mgr.dac_code, mtr::kDacMaxCode);
}

void test_estop_shutdown_and_sys_gap15_ack() {
    MockMotorManager mgr;
    mgr.init();

    // Cruising in Drive
    mgr.handle_drive_cmd(2000, can::Gear::D);
    mgr.tick();
    ASSERT_TRUE(mgr.relays.drive);
    ASSERT_TRUE(mgr.dac_code > 0);

    // Assert ESTOP
    mgr.trigger_estop();
    mgr.tick();

    // Relays and DAC killed
    ASSERT_TRUE(!mgr.relays.drive);
    ASSERT_TRUE(!mgr.relays.reverse);
    ASSERT_TRUE(!mgr.relays.ignition);
    ASSERT_EQ(mgr.dac_code, 0);

    // Check Gap #15 Feedback Frame: MTR must assert kMtrFaultEstopActive in 0x206
    can::Frame fbk_fr = mgr.build_feedback();
    can::gen::MtrMotorFbk fbk{};
    ASSERT_EQ(static_cast<int>(can::gen::decode_mtr_motor_fbk(fbk_fr.view(), fbk)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_TRUE((fbk.fault_flags & shared::kMtrFaultEstopActive) != 0);
}

void test_telemetry_frame_encoding() {
    MockMotorManager mgr;
    mgr.init();
    mgr.handle_drive_cmd(1234, can::Gear::D);
    mgr.tick();

    // 0x120 SYS_THROTTLE_STS
    can::Frame th_fr = mgr.build_throttle_sts();
    ASSERT_EQ(th_fr.id, 0x120u);
    ASSERT_EQ(th_fr.dlc, 2u);

    can::gen::SysThrottleSts sts{};
    ASSERT_EQ(static_cast<int>(can::gen::decode_sys_throttle_sts(th_fr.view(), sts)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(sts.speed_mmps, 1234);

    // 0x206 MTR_MOTOR_FBK
    can::Frame fbk_fr = mgr.build_feedback();
    ASSERT_EQ(fbk_fr.id, 0x206u);
    ASSERT_EQ(fbk_fr.dlc, 4u);

    can::gen::MtrMotorFbk fbk{};
    ASSERT_EQ(static_cast<int>(can::gen::decode_mtr_motor_fbk(fbk_fr.view(), fbk)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(fbk.actual_speed_mmps, 1234);
    ASSERT_EQ(fbk.gear_state, 1); // Drive
}

} // namespace

int main() {
    std::printf("========================================\n");
    std::printf("  MTR-STM32G431 Motor & Relay Unit Tests\n");
    std::printf("========================================\n");

    test_relay_mutual_exclusion();
    test_dac_throttle_curve_and_clamps();
    test_estop_shutdown_and_sys_gap15_ack();
    test_telemetry_frame_encoding();

    std::printf("----------------------------------------\n");
    std::printf("Tests Run: %d | Failures: %d\n", g_tests_run, g_tests_failed);
    if (g_tests_failed == 0) {
        std::printf("ALL MTR-STM32 TESTS PASSED!\n");
        return 0;
    }
    std::printf("SOME TESTS FAILED!\n");
    return 1;
}
