#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#include "protocol/compat/can.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"
#include "shared_config.h"
#include "config.h"
#include "sbus_parser.h"
#include "rc_decoder.h"

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
    if ((val) != (target)) { \
        std::printf("  FAIL [%s:%d]: val (%lld) != target (%lld)\n", __FILE__, __LINE__, \
                    static_cast<long long>(val), static_cast<long long>(target)); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT_NEAR(val, target, eps) do { \
    g_tests_run++; \
    if (std::abs((val) - (target)) > (eps)) { \
        std::printf("  FAIL [%s:%d]: val (%f) not near target (%f), eps=(%f)\n", \
                    __FILE__, __LINE__, static_cast<double>(val), static_cast<double>(target), static_cast<double>(eps)); \
        g_tests_failed++; \
    } \
} while(0)

// Helper to construct a raw 25-byte SBUS frame buffer
void pack_sbus_buffer(const uint16_t channels[16], uint8_t flags, uint8_t out_buf[25]) {
    out_buf[0] = rm::SbusParser::kHeaderByte;

    out_buf[1]  = static_cast<uint8_t>(channels[0] & 0x07FF);
    out_buf[2]  = static_cast<uint8_t>((channels[0] >> 8) | (channels[1] << 3));
    out_buf[3]  = static_cast<uint8_t>((channels[1] >> 5) | (channels[2] << 6));
    out_buf[4]  = static_cast<uint8_t>(channels[2] >> 2);
    out_buf[5]  = static_cast<uint8_t>((channels[2] >> 10) | (channels[3] << 1));
    out_buf[6]  = static_cast<uint8_t>((channels[3] >> 7) | (channels[4] << 4));
    out_buf[7]  = static_cast<uint8_t>((channels[4] >> 4) | (channels[5] << 7));
    out_buf[8]  = static_cast<uint8_t>(channels[5] >> 1);
    out_buf[9]  = static_cast<uint8_t>((channels[5] >> 9) | (channels[6] << 2));
    out_buf[10] = static_cast<uint8_t>((channels[6] >> 6) | (channels[7] << 5));
    out_buf[11] = static_cast<uint8_t>(channels[7] >> 3);
    out_buf[12] = static_cast<uint8_t>(channels[8] & 0x07FF);
    out_buf[13] = static_cast<uint8_t>((channels[8] >> 8) | (channels[9] << 3));
    out_buf[14] = static_cast<uint8_t>((channels[9] >> 5) | (channels[10] << 6));
    out_buf[15] = static_cast<uint8_t>(channels[10] >> 2);
    out_buf[16] = static_cast<uint8_t>((channels[10] >> 10) | (channels[11] << 1));
    out_buf[17] = static_cast<uint8_t>((channels[11] >> 7) | (channels[12] << 4));
    out_buf[18] = static_cast<uint8_t>((channels[12] >> 4) | (channels[13] << 7));
    out_buf[19] = static_cast<uint8_t>(channels[13] >> 1);
    out_buf[20] = static_cast<uint8_t>((channels[13] >> 9) | (channels[14] << 2));
    out_buf[21] = static_cast<uint8_t>((channels[14] >> 6) | (channels[15] << 5));
    out_buf[22] = static_cast<uint8_t>(channels[15] >> 3);

    out_buf[23] = flags;
    out_buf[24] = 0x00; // Standard footer
}

// ═══════════════════════════════════════════════════════════════════════
// Test Cases
// ═══════════════════════════════════════════════════════════════════════

void test_sbus_bit_unpacking_and_ranges() {
    uint16_t expected_channels[16] = {
        172, 992, 1811, 500, 1000, 1500, 2000, 300,
        1234, 567, 890, 172, 992, 1811, 1024, 2047
    };
    uint8_t flags = 0x01 | 0x02; // CH17 + CH18 active
    uint8_t raw_buf[25];
    pack_sbus_buffer(expected_channels, flags, raw_buf);

    rm::SbusFrame frame{};
    bool ok = rm::SbusParser::decode_buffer(raw_buf, frame);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(frame.ch17);
    ASSERT_TRUE(frame.ch18);
    ASSERT_FALSE(frame.frame_lost);
    ASSERT_FALSE(frame.failsafe);

    for (int i = 0; i < 16; ++i) {
        ASSERT_EQ(frame.channels[i], expected_channels[i]);
    }
}

void test_sbus_byte_stream_parser() {
    rm::SbusParser parser;
    uint16_t channels[16] = {992, 992, 172, 992, 172, 992, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172};
    uint8_t raw_buf[25];
    pack_sbus_buffer(channels, 0x00, raw_buf);

    // Feed some corrupted garbage bytes first
    rm::SbusFrame dummy{};
    ASSERT_FALSE(parser.parse_byte(0xAA, dummy));
    ASSERT_FALSE(parser.parse_byte(0x55, dummy));
    ASSERT_FALSE(parser.parse_byte(0x00, dummy));

    // Now feed legitimate 25-byte frame
    bool completed = false;
    rm::SbusFrame out{};
    for (size_t i = 0; i < 25; ++i) {
        bool res = parser.parse_byte(raw_buf[i], out);
        if (i == 24) {
            completed = res;
        } else {
            ASSERT_FALSE(res);
        }
    }
    ASSERT_TRUE(completed);
    ASSERT_EQ(out.channels[0], 992);
}

void test_graduated_link_states_and_failsafe() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992; // 1500us
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(1000); // UP (Disabled)
    frame.channels[rm::kChParkHold]      = rm::pulse_us_to_sbus(1000); // UP (Park/Hold)
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1000); // Precision
    frame.channels[rm::kChAutoRequest]   = rm::pulse_us_to_sbus(1000); // Manual

    uint32_t last_ms = 1000;

    // 1. Normal link (fresh: 20 ms dt <= 50 ms)
    auto snap = rm::decode_sbus_frame(frame, last_ms, 1020);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Normal);
    ASSERT_TRUE(snap.signal_valid);

    // 2. Degraded link (stale: 75 ms dt, 50 < dt <= 100 ms)
    snap = rm::decode_sbus_frame(frame, last_ms, 1075);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Degraded);
    ASSERT_TRUE(snap.signal_valid);

    // 3. Lost link (> 100 ms dt)
    snap = rm::decode_sbus_frame(frame, last_ms, 1105);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Lost);
    ASSERT_FALSE(snap.signal_valid);
    ASSERT_EQ(snap.target_speed_mmps, 0);

    // 4. Hardware failsafe flag asserted by receiver
    frame.failsafe = true;
    snap = rm::decode_sbus_frame(frame, last_ms, 1020);
    ASSERT_TRUE(snap.failsafe);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Lost);
    ASSERT_FALSE(snap.signal_valid);
    ASSERT_EQ(snap.target_speed_mmps, 0);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.001f);
}

void test_channel_plausibility_clusters() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChParkHold]      = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChAutoRequest]   = rm::pulse_us_to_sbus(1000);

    uint32_t now_ms = 1000;

    // Normal plausible frame
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.input_plausible);
    ASSERT_TRUE(snap.signal_valid);

    // Switch sitting in invalid dead zone between clusters (e.g. 1280us)
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1280);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.input_plausible);
    ASSERT_FALSE(snap.signal_valid); // Marked invalid due to implausible cluster
}

void test_steering_deadband_and_limits() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992; // 1500us
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChParkHold]      = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChAutoRequest]   = rm::pulse_us_to_sbus(1000);

    uint32_t now_ms = 1000;

    // 1. Center neutral
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.signal_valid);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.01f);

    // 2. Deadband: +/- 30us
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1525);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.01f);

    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1475);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.01f);

    // 3. Full Right (1950us -> +45.0 deg)
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1950);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, rm::kMaxSteerAngleDeg, 0.5f);

    // 4. Full Left (1050us -> -45.0 deg)
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1050);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, -rm::kMaxSteerAngleDeg, 0.5f);
}

void test_signed_velocity_and_speed_envelopes() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(2000); // SWA DOWN (Enable Req)
    frame.channels[rm::kChParkHold]      = rm::pulse_us_to_sbus(2000); // SWB DOWN (Drive)
    frame.channels[rm::kChAutoRequest]   = rm::pulse_us_to_sbus(1000); // Manual

    uint32_t now_ms = 1000;

    // 1. Precision Envelope (SWC <= 1250us -> 750 mm/s max fwd)
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChVelocity]      = rm::pulse_us_to_sbus(1950); // Full forward
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.drive_envelope == rm::DriveEnvelope::Precision);
    ASSERT_NEAR(snap.velocity_norm, 1.0f, 0.01f);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedPrecisionMaxMmps);

    // 2. Normal Envelope (SWC ~1500us -> 1800 mm/s max fwd)
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1500);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.drive_envelope == rm::DriveEnvelope::Normal);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedNormalMaxMmps);

    // 3. Fast Envelope (SWC >= 1750us -> 3000 mm/s max fwd)
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(2000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.drive_envelope == rm::DriveEnvelope::Fast);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedFastMaxMmps);

    // 4. Center deadband on velocity (1520us -> 0 mm/s)
    frame.channels[rm::kChVelocity] = rm::pulse_us_to_sbus(1520);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.velocity_norm, 0.0f, 0.001f);
    ASSERT_EQ(snap.target_speed_mmps, 0);
}

void test_strict_direction_reversal_guard() {
    rm::ReversalTracker rev{};
    uint32_t now_ms = 1000;
    bool locked = false;

    // 1. Driving forward: target speed = 1800 mm/s
    int32_t spd = rev.process_velocity(1.0f, 1800, 1000, 1500, now_ms, &locked);
    ASSERT_EQ(spd, 1800);
    ASSERT_FALSE(locked);

    // 2. Operator immediately pulls stick to full reverse (-1.0f)
    // Vehicle is still rolling forward at 800 mm/s (> 50 mm/s threshold)
    now_ms += 20;
    spd = rev.process_velocity(-1.0f, 1800, 1000, 800, now_ms, &locked);
    ASSERT_EQ(spd, 0); // Reverse locked out! Demand zero torque
    ASSERT_TRUE(locked);

    // 3. 500 ms passes (> 200 ms timeout), BUT vehicle is STILL rolling at 100 mm/s!
    // A timer alone MUST NEVER authorize reverse torque while moving forward!
    now_ms += 500;
    spd = rev.process_velocity(-1.0f, 1800, 1000, 100, now_ms, &locked);
    ASSERT_EQ(spd, 0); // Still locked!
    ASSERT_TRUE(locked);

    // 4. Vehicle finally comes to rest (speed = 20 mm/s <= 50 mm/s), but dwell timer just started
    now_ms += 50;
    spd = rev.process_velocity(-1.0f, 1800, 1000, 20, now_ms, &locked);
    ASSERT_EQ(spd, 0); // Still locked, dwell pending
    ASSERT_TRUE(locked);

    // 5. Vehicle stationary AND 200 ms neutral dwell elapsed: reverse authorized!
    now_ms += 205;
    spd = rev.process_velocity(-1.0f, 1800, 1000, 0, now_ms, &locked);
    ASSERT_EQ(spd, -1000); // Reverse torque now permitted!
    ASSERT_FALSE(locked);
}

void test_edge_qualified_arming_sequence() {
    rm::ArmingTracker arm{};
    uint32_t now_ms = 1000;

    // Scenario A: Boot up with SWA already DOWN (unsafe)
    arm.update(true, true, true, now_ms);
    ASSERT_FALSE(arm.is_armed()); // Must refuse to arm!

    // 600 ms passes, still DOWN
    now_ms += 600;
    arm.update(true, true, true, now_ms);
    ASSERT_FALSE(arm.is_armed()); // Still refused!

    // Operator observes T12D and flips SWA UP (Disabled)
    now_ms += 50;
    arm.update(false, true, true, now_ms);
    ASSERT_FALSE(arm.is_armed());

    // Operator now deliberately flips SWA DOWN with neutral stick -> ARMED!
    now_ms += 50;
    arm.update(true, true, true, now_ms);
    ASSERT_TRUE(arm.is_armed());

    // Operator flips SWA UP -> immediate disarm
    now_ms += 50;
    arm.update(false, true, true, now_ms);
    ASSERT_FALSE(arm.is_armed());

    // Scenario B: RF link lost while armed -> immediate disarm and require re-arm
    now_ms += 50;
    arm.update(true, true, true, now_ms);
    ASSERT_TRUE(arm.is_armed());

    // Link lost!
    now_ms += 50;
    arm.update(true, true, false, now_ms);
    ASSERT_FALSE(arm.is_armed());

    // Link recovers while SWA was left DOWN -> must NOT auto-arm!
    now_ms += 600;
    arm.update(true, true, true, now_ms);
    ASSERT_FALSE(arm.is_armed());
}

void test_park_hold_semantic_request() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(2000);
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChAutoRequest]   = rm::pulse_us_to_sbus(1000);

    uint32_t now_ms = 1000;

    // SWB UP (< 1500us) -> Park/Hold requested
    frame.channels[rm::kChParkHold] = rm::pulse_us_to_sbus(1000);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.park_hold_req);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kParkBrakeStrokeMm, 0.001f);
    ASSERT_TRUE(snap.gear == can::Gear::N);

    // SWB DOWN (>= 1500us) -> Drive requested
    frame.channels[rm::kChParkHold] = rm::pulse_us_to_sbus(2000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.park_hold_req);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);
}

void test_asymmetric_authority_request() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChParkHold]      = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChDriveEnvelope] = rm::pulse_us_to_sbus(1500);

    uint32_t now_ms = 1000;

    // SWD UP (< 1500us) -> Manual
    frame.channels[rm::kChAutoRequest] = rm::pulse_us_to_sbus(1000);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.auto_mode_req);

    // SWD DOWN (>= 1500us) -> Auto Request
    frame.channels[rm::kChAutoRequest] = rm::pulse_us_to_sbus(2000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.auto_mode_req);
}

void test_can_frame_encoding() {
    // 1. 0x169 VCU_SES_REQ
    can::custom::ses::Command ses_cmd{};
    ses_cmd.alignment_enable = true;
    ses_cmd.control_enable = true;
    ses_cmd.target_angle_raw = 30000; // 0.0 deg
    ses_cmd.target_speed_raw = 328;
    ses_cmd.rolling_counter = 5;
    can::Frame ses_fr;
    ASSERT_EQ(can::custom::ses::encode_command(ses_cmd, ses_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(ses_fr.id, 0x169u);
    ASSERT_EQ(ses_fr.dlc, 8u);

    // 2. 0x7B9 VCU_SEB_REQ
    can::custom::seb::Command seb_cmd{};
    seb_cmd.alignment_enable = true;
    seb_cmd.control_enable = true;
    seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
    seb_cmd.stroke_request_raw = 900; // 15.0 mm park hold
    seb_cmd.rolling_counter = 3;
    can::Frame seb_fr;
    ASSERT_EQ(can::custom::seb::encode_command(seb_cmd, seb_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(seb_fr.id, 0x7B9u);

    // 3. 0x204 RT_DRIVE_CMD
    can::gen::RtDriveCmd drive_cmd{};
    drive_cmd.motor_speed_mmps = 1800;
    drive_cmd.gear = static_cast<uint8_t>(can::Gear::D);
    can::Frame drive_fr;
    ASSERT_EQ(can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(drive_fr.id, 0x204u);

    // 4. 0x001 SAFETY_ESTOP (DLC 0)
    can::gen::SafetyEstop estop_msg{};
    can::Frame estop_fr;
    ASSERT_EQ(can::gen::encode_safety_estop(estop_msg, estop_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(estop_fr.id, 0x001u);
    ASSERT_EQ(estop_fr.dlc, 0u);
}

} // namespace

int main() {
    std::printf("====================================================\n");
    std::printf("  RM-ESP32-T12D TEST SUITE (Automotive HMI Gateway)\n");
    std::printf("====================================================\n");

    test_sbus_bit_unpacking_and_ranges();
    test_sbus_byte_stream_parser();
    test_graduated_link_states_and_failsafe();
    test_channel_plausibility_clusters();
    test_steering_deadband_and_limits();
    test_signed_velocity_and_speed_envelopes();
    test_strict_direction_reversal_guard();
    test_edge_qualified_arming_sequence();
    test_park_hold_semantic_request();
    test_asymmetric_authority_request();
    test_can_frame_encoding();

    std::printf("----------------------------------------------------\n");
    std::printf("Assertions: %d | Failures: %d\n", g_tests_run, g_tests_failed);
    if (g_tests_failed == 0) {
        std::printf(">>> ALL RM-ESP32-T12D TESTS PASSED! <<<\n\n");
        return 0;
    } else {
        std::printf(">>> SOME RM-ESP32-T12D TESTS FAILED! <<<\n\n");
        return 1;
    }
}

