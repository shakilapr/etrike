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
#include "can_emitter.h"

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

    // Feed corrupted bytes first
    rm::SbusFrame dummy{};
    ASSERT_FALSE(parser.parse_byte(0xAA, dummy));
    ASSERT_FALSE(parser.parse_byte(0x55, dummy));
    ASSERT_FALSE(parser.parse_byte(0x00, dummy));

    // Feed legitimate 25-byte frame
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
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1000); // UP (Disabled)
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1000); // UP (Released)
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(1500); // Neutral
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000); // Manual

    // 0. Boot Guard (last_frame_ms == 0) -> Lost
    auto snap = rm::decode_sbus_frame(frame, 0, 20);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Lost);
    ASSERT_FALSE(snap.signal_valid);

    uint32_t last_ms = 1000;

    // 1. Normal link (fresh: 20 ms dt <= 150 ms)
    snap = rm::decode_sbus_frame(frame, last_ms, 1020);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Normal);
    ASSERT_TRUE(snap.signal_valid);

    // 2. Hardware frame lost flag (receiver reports single packet drop)
    frame.frame_lost = true;
    snap = rm::decode_sbus_frame(frame, last_ms, 1020);
    ASSERT_TRUE(snap.frame_lost);
    ASSERT_TRUE(snap.link_state == rm::LinkState::Degraded);
    ASSERT_TRUE(snap.signal_valid);
    frame.frame_lost = false;

    // 3. Lost link (> 150 ms dt)
    snap = rm::decode_sbus_frame(frame, last_ms, 1160);
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
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kParkBrakeStrokeMm, 0.001f);
}

void test_electrical_pulse_limits() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000);

    uint32_t now_ms = 1000;

    // Normal plausible frame
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.input_plausible);
    ASSERT_TRUE(snap.signal_valid);

    // Pulses in intermediate regions (e.g. 1450us) are valid and NOT falsely rejected!
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1450);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.input_plausible);
    ASSERT_TRUE(snap.signal_valid);
    ASSERT_FALSE(snap.drive_enable_req); // 1450 < 1500 -> OFF

    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1550);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.input_plausible);
    ASSERT_TRUE(snap.signal_valid);
    ASSERT_TRUE(snap.drive_enable_req); // 1550 >= 1500 -> ON
}

void test_steering_deadband_and_limits() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992; // 1500us
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000);

    uint32_t now_ms = 1000;

    // 1. Center neutral
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.signal_valid);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.01f);

    // 2. Inside deadband: +/- 30us
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1525);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.01f);

    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1475);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.01f);

    // 3. Just outside deadband (1535us -> offset 35us): smooth continuous ramp
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1535);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    // norm = (35 - 30) / (450 - 30) = 5 / 420 = 0.0119 -> ~0.54 deg (no 3.1 deg step jump!)
    ASSERT_NEAR(snap.steering_deg, 0.54f, 0.15f);

    // 4. Full Right (1950us -> +45.0 deg)
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1950);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, rm::kMaxSteerAngleDeg, 0.1f);

    // 5. Full Left (1050us -> -45.0 deg)
    frame.channels[rm::kChSteering] = rm::pulse_us_to_sbus(1050);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.steering_deg, -rm::kMaxSteerAngleDeg, 0.1f);
}

void test_throttle_and_gear_combinations() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(2000); // SWD DOWN (Enable)
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1500); // SWB MID (Park Released)
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000); // SWA UP (BARE)
    frame.channels[rm::kChAuxVra]      = rm::pulse_us_to_sbus(2000); // VRA 100% Speed Governor

    uint32_t now_ms = 1000;

    // 1. Drive (SWC DOWN = 2000us) + Full Throttle (1950us) -> +3000 mm/s
    frame.channels[rm::kChGear]     = rm::pulse_us_to_sbus(2000);
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1950);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.gear == can::Gear::D);
    ASSERT_NEAR(snap.throttle_norm, 1.0f, 0.01f);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedFwdMaxMmps); // +3000

    // 2. Drive + Mid Throttle (1500us) -> ~46% (~1378 mm/s with deadband 1160us)
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1500);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.throttle_norm, 0.46f, 0.02f);
    ASSERT_NEAR(static_cast<float>(snap.target_speed_mmps), 1378.0f, 50.0f);

    // 3. Drive + Throttle Idle Deadband (1100us <= 1120us) -> 0 mm/s
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1100);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.throttle_norm, 0.0f, 0.001f);
    ASSERT_EQ(snap.target_speed_mmps, 0);

    // 4. Reverse (SWC UP = 1000us) + Full Throttle (1950us) -> -500 mm/s
    frame.channels[rm::kChGear]     = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1950);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.gear == can::Gear::R);
    ASSERT_NEAR(snap.velocity_norm, -1.0f, 0.01f);
    ASSERT_EQ(snap.target_speed_mmps, -rm::kSpeedRevMaxMmps); // -500

    // 5. Neutral (SWC MID = 1500us) + Full Throttle -> 0 mm/s
    frame.channels[rm::kChGear]     = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1950);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.gear == can::Gear::N);
    ASSERT_EQ(snap.target_speed_mmps, 0);
}

void test_service_and_backup_braking() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(2000);
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1500); // SWB MID (Released)
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(2000); // Drive
    frame.channels[rm::kChThrottle]    = rm::pulse_us_to_sbus(1950); // Full throttle
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000); // SWA UP (BARE)

    uint32_t now_ms = 1000;

    // 1. Right Stick inside deadband zone (1500us, 1400us, 1600us) -> 0.0mm brake
    frame.channels[rm::kChBrake] = rm::pulse_us_to_sbus(1500);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);
    ASSERT_NEAR(snap.throttle_norm, 1.0f, 0.01f);

    frame.channels[rm::kChBrake] = rm::pulse_us_to_sbus(1600); // within +150us deadband
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);

    frame.channels[rm::kChBrake] = rm::pulse_us_to_sbus(1400); // within -150us deadband
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);

    // 2. Right Stick Brake pushed forward (1950us -> full 27.0mm)
    frame.channels[rm::kChBrake] = rm::pulse_us_to_sbus(1950);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.1f);
    ASSERT_NEAR(snap.throttle_norm, 0.0f, 0.001f);
    ASSERT_EQ(snap.target_speed_mmps, 0);

    // 3. Right Stick Brake pulled down (1050us -> full 27.0mm)
    frame.channels[rm::kChBrake] = rm::pulse_us_to_sbus(1050);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.1f);
    ASSERT_NEAR(snap.throttle_norm, 0.0f, 0.001f);
    ASSERT_EQ(snap.target_speed_mmps, 0);

    // 4. Right Stick Brake released, VRA Aux Knob turned past center (1950us)
    frame.channels[rm::kChBrake]  = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChAuxVra] = rm::pulse_us_to_sbus(1950);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f); // No brake from knob!
    ASSERT_NEAR(snap.aux_vra, 0.95f, 0.1f);          // Knob maps to aux analog / governor
    ASSERT_NEAR(snap.throttle_norm, 1.0f, 0.01f);    // Throttle NOT cut
    ASSERT_NEAR(static_cast<float>(snap.target_speed_mmps), static_cast<float>(rm::kSpeedFwdMaxMmps) * 0.94f, 100.0f);

    // 5. Knobs centered (1500us -> 50% governor)
    frame.channels[rm::kChAuxVra] = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChAuxVrb] = rm::pulse_us_to_sbus(1500);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);
    ASSERT_NEAR(snap.throttle_norm, 1.0f, 0.01f);
    ASSERT_NEAR(static_cast<float>(snap.target_speed_mmps), static_cast<float>(rm::kSpeedFwdMaxMmps) * 0.5f, 50.0f); // 1500 mm/s
}

void test_park_hold_semantic_request() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(2000); // SWD DOWN (Enabled)
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(2000); // Drive
    frame.channels[rm::kChThrottle]    = rm::pulse_us_to_sbus(1950); // Full throttle
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000); // SWA UP (BARE)
    frame.channels[rm::kChAuxVra]      = rm::pulse_us_to_sbus(2000); // VRA 100% Speed Governor

    uint32_t now_ms = 1000;

    // SWB UP (<= 1300us) -> Park Engaged (15mm holding stroke, Neutral gear, 0 mm/s)
    frame.channels[rm::kChParkHold] = rm::pulse_us_to_sbus(1000);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.park_hold_req);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kParkBrakeStrokeMm, 0.001f); // 15.0 mm
    ASSERT_TRUE(snap.gear == can::Gear::N);
    ASSERT_EQ(snap.target_speed_mmps, 0);

    // SWB MID (1500us > 1300us) -> Park Released (0mm stroke, full drive allowed)
    frame.channels[rm::kChParkHold] = rm::pulse_us_to_sbus(1500);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.park_hold_req);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);
    ASSERT_TRUE(snap.gear == can::Gear::D);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedFwdMaxMmps);

    // SWB DOWN (2000us > 1300us) -> Park Released (0mm stroke, full drive allowed)
    frame.channels[rm::kChParkHold] = rm::pulse_us_to_sbus(2000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.park_hold_req);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);
    ASSERT_TRUE(snap.gear == can::Gear::D);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedFwdMaxMmps);
}

void test_drive_enable_direct_switch() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1500); // SWB MID (Park Released)
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(2000);
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000);

    uint32_t now_ms = 1000;

    // SWD UP (< 1500us) -> Drive Disabled
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1000);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.drive_enable_req);

    // SWD DOWN (>= 1500us) -> Drive Enabled (Immediate, direct)
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(2000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.drive_enable_req);

    // SWD UP -> Drive Disabled
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_FALSE(snap.drive_enable_req);
}

void test_operating_mode_switch_decoding() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable] = rm::pulse_us_to_sbus(1000);
    frame.channels[rm::kChParkHold]    = rm::pulse_us_to_sbus(1500);
    frame.channels[rm::kChGear]        = rm::pulse_us_to_sbus(1500);

    uint32_t now_ms = 1000;

    // 1. SWA UP (<= 1300us, e.g. 1000us) -> OperatingMode::Bare
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000);
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.op_mode == rm::OperatingMode::Bare);
    ASSERT_TRUE(std::string_view(rm::mode_name(snap.op_mode)) == "BARE");

    // 2. SWA MID (1301..1699us, e.g. 1500us) -> OperatingMode::Sys
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1500);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.op_mode == rm::OperatingMode::Sys);
    ASSERT_TRUE(std::string_view(rm::mode_name(snap.op_mode)) == "SYS");

    // 3. SWA DOWN (>= 1700us, e.g. 2000us) -> OperatingMode::Rt
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(2000);
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_TRUE(snap.op_mode == rm::OperatingMode::Rt);
    ASSERT_TRUE(std::string_view(rm::mode_name(snap.op_mode)) == "RT");

    // 4. Link lost -> Safe fallback to OperatingMode::Bare
    snap = rm::decode_sbus_frame(frame, 0, now_ms);
    ASSERT_FALSE(snap.signal_valid);
    ASSERT_TRUE(snap.op_mode == rm::OperatingMode::Bare);
}

void test_can_emitter_three_modes() {
    rm::CanEmitter emitter;
    std::vector<can::Frame> emitted;
    auto send_fn = [&](const can::Frame& fr) {
        emitted.push_back(fr);
        return true;
    };

    rm::RcSnapshot snap{};
    snap.signal_valid = true;
    snap.drive_enable_req = true;
    snap.park_hold_req = false;
    snap.gear = can::Gear::D;
    snap.target_speed_mmps = 2500;
    snap.steering_deg = 20.0f;
    snap.brake_stroke_mm = 13.5f;

    // ── Test Mode 1: BARE ──
    snap.op_mode = rm::OperatingMode::Bare;
    emitted.clear();
    emitter.emit_cluster(snap, 0, send_fn); // tick 0 triggers 10Hz messages too

    bool has_ses = false;
    bool has_seb = false;
    bool has_drive = false;
    bool has_sys_mode = false;
    bool has_sys_pwr = false;
    bool has_hmi_mode = false;
    bool has_rt_hb = false;
    bool has_host_steer = false;

    for (const auto& fr : emitted) {
        if (fr.id == 0x169u) has_ses = true;
        if (fr.id == 0x7B9u) has_seb = true;
        if (fr.id == 0x204u) has_drive = true;
        if (fr.id == 0x110u) has_sys_mode = true;
        if (fr.id == 0x113u) has_sys_pwr = true;
        if (fr.id == 0x111u) has_hmi_mode = true;
        if (fr.id == 0x7FDu) has_rt_hb = true;
        if (fr.id == 0x303u) has_host_steer = true;
    }
    ASSERT_TRUE(has_ses);
    ASSERT_TRUE(has_seb);
    ASSERT_TRUE(has_drive);
    ASSERT_TRUE(has_sys_mode);
    ASSERT_TRUE(has_sys_pwr);
    ASSERT_FALSE(has_hmi_mode);
    ASSERT_FALSE(has_rt_hb);
    ASSERT_FALSE(has_host_steer);

    // ── Test Mode 2: SYS ──
    snap.op_mode = rm::OperatingMode::Sys;
    emitted.clear();
    emitter.emit_cluster(snap, 0, send_fn); // tick 0 triggers 10Hz and 2Hz (500ms)

    has_ses = false;
    has_seb = false;
    has_drive = false;
    has_sys_mode = false;
    has_sys_pwr = false;
    has_hmi_mode = false;
    bool has_hmi_pwr = false;
    has_rt_hb = false;
    bool has_host_hb = false;

    for (const auto& fr : emitted) {
        if (fr.id == 0x169u) has_ses = true;
        if (fr.id == 0x7B9u) has_seb = true;
        if (fr.id == 0x204u) has_drive = true;
        if (fr.id == 0x110u) has_sys_mode = true;
        if (fr.id == 0x113u) has_sys_pwr = true;
        if (fr.id == 0x111u) has_hmi_mode = true;
        if (fr.id == 0x112u) has_hmi_pwr = true;
        if (fr.id == 0x7FDu) has_rt_hb = true;
        if (fr.id == 0x7FCu) has_host_hb = true;
    }
    ASSERT_TRUE(has_ses);
    ASSERT_TRUE(has_seb);
    ASSERT_TRUE(has_drive);
    ASSERT_FALSE(has_sys_mode); // suppressed in SYS mode
    ASSERT_FALSE(has_sys_pwr);  // suppressed in SYS mode
    ASSERT_TRUE(has_hmi_mode);
    ASSERT_TRUE(has_hmi_pwr);
    ASSERT_TRUE(has_rt_hb);
    ASSERT_FALSE(has_host_hb);

    // ── Test Mode 3: RT ──
    snap.op_mode = rm::OperatingMode::Rt;
    emitted.clear();
    emitter.emit_cluster(snap, 0, send_fn);

    has_ses = false;
    has_drive = false;
    has_host_steer = false;
    bool has_host_brake = false;
    bool has_host_drive = false;
    has_hmi_mode = true;
    has_host_hb = false;

    for (const auto& fr : emitted) {
        if (fr.id == 0x169u) has_ses = true;
        if (fr.id == 0x204u) has_drive = true;
        if (fr.id == 0x303u) has_host_steer = true;
        if (fr.id == 0x301u) has_host_brake = true;
        if (fr.id == 0x300u) has_host_drive = true;
        if (fr.id == 0x7FCu) has_host_hb = true;
    }
    ASSERT_FALSE(has_ses);
    ASSERT_FALSE(has_drive);
    ASSERT_TRUE(has_host_steer);
    ASSERT_TRUE(has_host_brake);
    ASSERT_TRUE(has_host_drive);
    ASSERT_TRUE(has_host_hb);
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
    drive_cmd.motor_speed_mmps = 3000;
    drive_cmd.gear = static_cast<uint8_t>(can::Gear::D);
    can::Frame drive_fr;
    ASSERT_EQ(can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(drive_fr.id, 0x204u);

    // 4. 0x110 SYS_MODE_CMD
    can::gen::SysModeCmd mode_cmd{};
    mode_cmd.mode = true;
    mode_cmd.rolling_counter = 42;
    can::Frame mode_fr;
    ASSERT_EQ(can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(mode_fr.id, 0x110u);

    // 5. 0x113 SYS_PWR_CMD
    can::gen::SysPwrCmd pwr_cmd{};
    pwr_cmd.power_state = true;
    pwr_cmd.rolling_counter = 12;
    can::Frame pwr_fr;
    ASSERT_EQ(can::gen::encode_sys_pwr_cmd(pwr_cmd, pwr_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(pwr_fr.id, 0x113u);

    // 6. 0x001 SAFETY_ESTOP (DLC 0)
    can::gen::SafetyEstop estop_msg{};
    can::Frame estop_fr;
    ASSERT_EQ(can::gen::encode_safety_estop(estop_msg, estop_fr), can::gen::CodecStatus::Ok);
    ASSERT_EQ(estop_fr.id, 0x001u);
    ASSERT_EQ(estop_fr.dlc, 0u);
}

void test_vra_speed_governor_scaling() {
    rm::SbusFrame frame{};
    for (int i = 0; i < 16; ++i) frame.channels[i] = 992;
    frame.channels[rm::kChDriveEnable]   = rm::pulse_us_to_sbus(2000); // SWD DOWN (Enable)
    frame.channels[rm::kChParkHold]      = rm::pulse_us_to_sbus(1500); // SWB MID (Park Released)
    frame.channels[rm::kChOperatingMode] = rm::pulse_us_to_sbus(1000); // SWA UP (BARE)

    uint32_t now_ms = 1000;

    // 1. Full Throttle in Drive (D) with VRA at 100% (2000us) -> full +3000 mm/s
    frame.channels[rm::kChGear]     = rm::pulse_us_to_sbus(2000); // Drive
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1950); // Full Throttle
    frame.channels[rm::kChAuxVra]   = rm::pulse_us_to_sbus(2000); // 100% Governor
    auto snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.aux_vra, 1.0f, 0.01f);
    ASSERT_EQ(snap.target_speed_mmps, rm::kSpeedFwdMaxMmps); // 3000

    // 2. Full Throttle in Drive (D) with VRA at 50% (1500us) -> 1500 mm/s (5.4 km/h)
    frame.channels[rm::kChAuxVra] = rm::pulse_us_to_sbus(1500); // 50% Governor
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.aux_vra, 0.5f, 0.02f);
    ASSERT_NEAR(static_cast<float>(snap.target_speed_mmps), 1500.0f, 50.0f);

    // 3. Mid Throttle in Drive (D) (~46%) with VRA at 50% -> ~46% of 1500 = ~689 mm/s
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1500); // Mid Throttle
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(static_cast<float>(snap.target_speed_mmps), 689.0f, 40.0f);

    // 4. Full Throttle in Drive (D) with VRA at 0% (1000us) -> 0 mm/s
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1950); // Full Throttle
    frame.channels[rm::kChAuxVra]   = rm::pulse_us_to_sbus(1000); // 0% Governor
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(snap.aux_vra, 0.0f, 0.01f);
    ASSERT_EQ(snap.target_speed_mmps, 0);

    // 5. Full Throttle in Reverse (R) with VRA at 100% (2000us) -> -500 mm/s
    frame.channels[rm::kChGear]     = rm::pulse_us_to_sbus(1000); // Reverse
    frame.channels[rm::kChThrottle] = rm::pulse_us_to_sbus(1950); // Full Throttle
    frame.channels[rm::kChAuxVra]   = rm::pulse_us_to_sbus(2000); // 100% Governor
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_EQ(snap.target_speed_mmps, -rm::kSpeedRevMaxMmps); // -500

    // 6. Full Throttle in Reverse (R) with VRA at 50% (1500us) -> -250 mm/s
    frame.channels[rm::kChAuxVra] = rm::pulse_us_to_sbus(1500); // 50% Governor
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_NEAR(static_cast<float>(snap.target_speed_mmps), -250.0f, 15.0f);

    // 7. Full Throttle in Reverse (R) with VRA at 0% (1000us) -> 0 mm/s
    frame.channels[rm::kChAuxVra] = rm::pulse_us_to_sbus(1000); // 0% Governor
    snap = rm::decode_sbus_frame(frame, now_ms, now_ms);
    ASSERT_EQ(snap.target_speed_mmps, 0);
}

} // namespace

int main() {
    std::printf("====================================================\n");
    std::printf("  RM-ESP32-T12D TEST SUITE (Operator Input Gateway)\n");
    std::printf("====================================================\n");

    test_sbus_bit_unpacking_and_ranges();
    test_sbus_byte_stream_parser();
    test_graduated_link_states_and_failsafe();
    test_electrical_pulse_limits();
    test_steering_deadband_and_limits();
    test_throttle_and_gear_combinations();
    test_service_and_backup_braking();
    test_park_hold_semantic_request();
    test_drive_enable_direct_switch();
    test_operating_mode_switch_decoding();
    test_vra_speed_governor_scaling();
    test_can_emitter_three_modes();
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
