#include <cstdint>
#include <cstdio>
#include <cmath>
#include "protocol/compat/can.hpp"
#include "rm-esp32/src/config.h"
#include "rm-esp32/src/rc_decoder.h"

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

#define ASSERT_NEAR(val, target, eps) do { \
    g_tests_run++; \
    if (std::abs((val) - (target)) > (eps)) { \
        std::printf("FAIL [%s:%d]: val %f not near %f (eps %f)\n", __FILE__, __LINE__, static_cast<double>(val), static_cast<double>(target), static_cast<double>(eps)); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_EQ(val, target) do { \
    g_tests_run++; \
    if ((val) != (target)) { \
        std::printf("FAIL [%s:%d]: val != target\n", __FILE__, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

void test_steering_deadband_and_limits() {
    uint32_t raw_us[rm::kNumRcChannels] = {1500, 1500, 1500, 1500, 1000, 1500};
    uint32_t last_edge_ms[rm::kNumRcChannels] = {100, 100, 100, 100, 100, 100};
    uint32_t now_ms = 110;

    // 1. Center neutral
    auto snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_TRUE(snap.signal_valid);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);

    // 2. Center within deadband (+/- 30us: 1525us should remain 0)
    raw_us[0] = 1525;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);

    raw_us[0] = 1475;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);

    // 3. Full Right (1950us -> +kMaxSteerAngleDeg)
    raw_us[0] = 1950;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, rm::kMaxSteerAngleDeg, 0.1f);

    // 4. Full Left (1050us -> -kMaxSteerAngleDeg)
    raw_us[0] = 1050;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, -rm::kMaxSteerAngleDeg, 0.1f);
}

void test_brake_stroke_proportionality() {
    uint32_t raw_us[rm::kNumRcChannels] = {1500, 1500, 1500, 1500, 1000, 1500};
    uint32_t last_edge_ms[rm::kNumRcChannels] = {100, 100, 100, 100, 100, 100};
    uint32_t now_ms = 110;

    // Released at center
    auto snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);

    // Full brake trigger pull (1970us -> 27.0mm)
    raw_us[1] = 1970;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 27.0f, 0.1f);

    // Half pull (~1745us -> ~13.5mm)
    raw_us[1] = 1745;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 13.5f, 0.5f);
}

void test_flysky_switches_gear_and_ignition() {
    uint32_t raw_us[rm::kNumRcChannels] = {1500, 1500, 1500, 1500, 1000, 1500};
    uint32_t last_edge_ms[rm::kNumRcChannels] = {100, 100, 100, 100, 100, 100};
    uint32_t now_ms = 110;

    // SWB: Ignition OFF (1000us)
    raw_us[4] = 1000;
    auto snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_TRUE(!snap.ignition);

    // SWB: Ignition ON (2000us)
    raw_us[4] = 2000;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_TRUE(snap.ignition);

    // SWC: Gear Selection
    // MID (1500us -> Park/Neutral)
    raw_us[5] = 1500;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::N));

    // UP (1000us -> Reverse)
    raw_us[5] = 1000;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::R));

    // DOWN (2000us -> Drive)
    raw_us[5] = 2000;
    snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::D));
}

void test_deadman_signal_loss_failsafe() {
    uint32_t raw_us[rm::kNumRcChannels] = {1500, 1500, 1500, 1500, 2000, 2000};
    uint32_t last_edge_ms[rm::kNumRcChannels] = {100, 100, 100, 100, 100, 100};
    // Exceed timeout (now = 250ms > 100ms + 100ms threshold)
    uint32_t now_ms = 250;

    auto snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    ASSERT_TRUE(!snap.signal_valid);
    // On signal loss, brake must assert maximum stroke (27.0mm)
    ASSERT_NEAR(snap.brake_stroke_mm, 27.0f, 0.001f);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);
    ASSERT_TRUE(!snap.ignition);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::N));
}

void test_canonical_can_encoding() {
    // Test 1: VCU_SES_REQ (0x169) - Neutral Center (30000 raw = 0.0 deg)
    can::custom::ses::Command ses_cmd{};
    ses_cmd.alignment_enable = true;
    ses_cmd.control_enable = true;
    ses_cmd.target_angle_raw = static_cast<int16_t>(rm::kSbwAngleOffset); // 30000
    ses_cmd.target_speed_raw = 328;
    ses_cmd.rolling_counter = 5;

    can::Frame ses_fr;
    ASSERT_EQ(static_cast<int>(can::custom::ses::encode_command(ses_cmd, ses_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(ses_fr.id, 0x169u);
    ASSERT_EQ(ses_fr.dlc, 8u);
    // Byte 2-3 little endian: 30000 = 0x7530 -> data[2]=0x30, data[3]=0x75
    ASSERT_EQ(ses_fr.data[2], 0x30);
    ASSERT_EQ(ses_fr.data[3], 0x75);
    ASSERT_TRUE(ses_fr.data[7] != 0); // XOR checksum

    // Test 1b: Full Left (29550 raw = -45.0 deg) & Full Right (30450 raw = +45.0 deg)
    ses_cmd.target_angle_raw = rm::kMinSteerRaw; // 29550
    ASSERT_EQ(static_cast<int>(can::custom::ses::encode_command(ses_cmd, ses_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    // 29550 = 0x736E -> data[2]=0x6E, data[3]=0x73
    ASSERT_EQ(ses_fr.data[2], 0x6E);
    ASSERT_EQ(ses_fr.data[3], 0x73);

    ses_cmd.target_angle_raw = rm::kMaxSteerRaw; // 30450
    ASSERT_EQ(static_cast<int>(can::custom::ses::encode_command(ses_cmd, ses_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    // 30450 = 0x76F2 -> data[2]=0xF2, data[3]=0x76
    ASSERT_EQ(ses_fr.data[2], 0xF2);
    ASSERT_EQ(ses_fr.data[3], 0x76);

    // Test 2: VCU_SEB_REQ (0x7B9) - Released (600 raw = 0mm) & Max (1140 raw = 27mm)
    can::custom::seb::Command seb_cmd{};
    seb_cmd.alignment_enable = true;
    seb_cmd.control_enable = true;
    seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
    seb_cmd.stroke_request_raw = 600; // 0mm
    seb_cmd.rolling_counter = 3;

    can::Frame seb_fr;
    ASSERT_EQ(static_cast<int>(can::custom::seb::encode_command(seb_cmd, seb_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(seb_fr.id, 0x7B9u);
    ASSERT_EQ(seb_fr.dlc, 8u);
    // Byte 2-3 little endian: 600 = 0x0258 -> data[2]=0x58, data[3]=0x02
    ASSERT_EQ(seb_fr.data[2], 0x58);
    ASSERT_EQ(seb_fr.data[3], 0x02);
    ASSERT_TRUE(seb_fr.data[7] != 0);

    seb_cmd.stroke_request_raw = 1140; // 27mm max emergency brake
    ASSERT_EQ(static_cast<int>(can::custom::seb::encode_command(seb_cmd, seb_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    // 1140 = 0x0474 -> data[2]=0x74, data[3]=0x04
    ASSERT_EQ(seb_fr.data[2], 0x74);
    ASSERT_EQ(seb_fr.data[3], 0x04);

    // Test 3: HMI_MODE_REQ (0x111) & HMI_PWR_REQ (0x112)
    can::gen::HmiModeReq mode_msg{1, 42};
    can::Frame mode_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_hmi_mode_req(mode_msg, mode_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(mode_fr.id, 0x111u);
    ASSERT_EQ(mode_fr.dlc, 2u);

    can::gen::HmiPwrReq pwr_msg{1, 10};
    can::Frame pwr_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_hmi_pwr_req(pwr_msg, pwr_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(pwr_fr.id, 0x112u);
    ASSERT_EQ(pwr_fr.dlc, 2u);

    // Test 4: SAFETY_ESTOP (0x001) DLC 0
    can::gen::SafetyEstop estop_msg{};
    can::Frame estop_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_safety_estop(estop_msg, estop_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(estop_fr.id, 0x001u);
    ASSERT_EQ(estop_fr.dlc, 0u);
}

} // namespace

int main() {
    std::printf("========================================\n");
    std::printf("  RM-ESP32 Receiver & CAN Protocol Tests\n");
    std::printf("========================================\n");

    test_steering_deadband_and_limits();
    test_brake_stroke_proportionality();
    test_flysky_switches_gear_and_ignition();
    test_deadman_signal_loss_failsafe();
    test_canonical_can_encoding();

    std::printf("----------------------------------------\n");
    std::printf("Tests Run: %d | Failures: %d\n", g_tests_run, g_tests_failed);
    if (g_tests_failed == 0) {
        std::printf("ALL RM-ESP32 TESTS PASSED!\n");
        return 0;
    }
    std::printf("SOME TESTS FAILED!\n");
    return 1;
}
