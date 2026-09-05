#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <cstring>

#include "protocol/compat/can.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "protocol/generated/cpp/etrike_protocol.hpp"

// RM signal decoder & MTR motor manager logic
#include "rm-esp32/src/config.h"
#include "rm-esp32/src/rc_decoder.h"
#include "mtr-stm32/src/config.h"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"

namespace generated = etrike::protocol::generated;
namespace codecs = etrike::protocol::codecs;

static int g_pass = 0;
static int g_fail = 0;

#define TEST_CHECK(cond, msg) do { \
    if (cond) { \
        g_pass++; \
    } else { \
        std::fprintf(stderr, "  [FAIL] %s (line %d)\n", msg, __LINE__); \
        g_fail++; \
    } \
} while (0)

#define TEST_CHECK_EQ(actual, expected, msg) do { \
    auto _a = (actual); \
    auto _e = (expected); \
    if (_a == _e) { \
        g_pass++; \
    } else { \
        std::fprintf(stderr, "  [FAIL] %s: actual %lld != expected %lld (line %d)\n", \
                     msg, static_cast<long long>(_a), static_cast<long long>(_e), __LINE__); \
        g_fail++; \
    } \
} while (0)

// Authority frames (0x110/0x113) require a baseline frame then an advancing
// frame before MotorManager's StreamValidity accepts them. Each stream keeps
// its own monotonic counter.
static uint8_t g_tam_mode_ctr = 0;
static uint8_t g_tam_pwr_ctr = 0;
static void auth_mode(mtr::MotorManager& m, can::Mode mode, uint32_t now) {
    generated::SysModeCmd c0{static_cast<uint8_t>(mode), g_tam_mode_ctr++};
    etrike::protocol::Frame f0; generated::encode_sys_mode_cmd(c0, f0); m.handle_frame(f0, now);
    generated::SysModeCmd c1{static_cast<uint8_t>(mode), g_tam_mode_ctr++};
    etrike::protocol::Frame f1; generated::encode_sys_mode_cmd(c1, f1); m.handle_frame(f1, now);
}
static void auth_power(mtr::MotorManager& m, bool on, uint32_t now) {
    generated::SysPwrCmd c0{on, g_tam_pwr_ctr++};
    etrike::protocol::Frame f0; generated::encode_sys_pwr_cmd(c0, f0); m.handle_frame(f0, now);
    generated::SysPwrCmd c1{on, g_tam_pwr_ctr++};
    etrike::protocol::Frame f1; generated::encode_sys_pwr_cmd(c1, f1); m.handle_frame(f1, now);
}

// ============================================================================
// Test Suite 1: Flow A — Autonomous Mode (Jetson -> RT -> SES/MTR/SEB)
// ============================================================================
static void test_flow_a_autonomous_pipeline() {
    std::printf("\n--- Flow A: Autonomous Mode Pipeline ---\n");

    // 1. Host Drive Command (0x300) from Jetson
    generated::HostDriveCmd host_cmd{};
    host_cmd.speed_mmps = 2200;       // 2.2 m/s
    host_cmd.yaw_rate_mrad_s = 200;   // 0.200 rad/s (~0.09 rad/m curvature at 2.2 m/s)
    host_cmd.gear = 1;                // Drive
    etrike::protocol::Frame frame_300{};
    generated::encode(host_cmd, frame_300);

    TEST_CHECK_EQ(frame_300.id, 0x300u, "0x300 CAN ID matches HOST_DRIVE_CMD");
    TEST_CHECK_EQ(frame_300.dlc, 8u, "0x300 DLC is 8");

    // Decode on RT gateway
    generated::HostDriveCmd decoded_host_cmd{};
    auto status_300 = generated::decode(frame_300.view(), decoded_host_cmd);
    TEST_CHECK(status_300 == etrike::protocol::CodecStatus::Ok, "Decode 0x300 succeeds");
    TEST_CHECK_EQ(decoded_host_cmd.speed_mmps, 2200, "Decoded speed matches 2200 mm/s");
    TEST_CHECK_EQ(decoded_host_cmd.yaw_rate_mrad_s, 200, "Decoded yaw rate matches 200 mrad/s");

    // 2. RT Kinematics transformation (Tricycle Model)
    // Curvature kappa = yaw_rate / speed = 0.2 / 2.2 ~= 0.0909 rad/m
    const float speed_mps = decoded_host_cmd.speed_mmps / 1000.0f;
    const float yaw_rate_rads = decoded_host_cmd.yaw_rate_mrad_s / 1000.0f;
    const float kappa = yaw_rate_rads / speed_mps;
    const float L = 1.35f;
    const float W = 0.88f;

    // delta = atan(L * kappa / (1 - 0.5 * W * kappa))
    float delta_rad = std::atan(L * kappa / (1.0f - 0.5f * W * kappa));
    float delta_deg = delta_rad * 180.0f / 3.14159265358979323846f;

    // Verify calculated steering angle is within safety bounds [-450, 450]
    TEST_CHECK(delta_deg > 0.0f && delta_deg < 45.0f, "Tricycle front fork angle is physically valid (~7.5 deg)");

    // Encode SES Steer Command (0x169)
    codecs::ses::Command ses_cmd{};
    ses_cmd.alignment_enable = true;
    ses_cmd.control_enable = true;
    ses_cmd.target_angle_raw = static_cast<int16_t>(std::round(delta_deg * 10.0f)); // 0.1 deg/LSB
    ses_cmd.target_speed_raw = 328;
    ses_cmd.rolling_counter = 1;
    etrike::protocol::Frame frame_169{};
    auto status_169 = codecs::ses::encode_command(ses_cmd, frame_169);
    TEST_CHECK(status_169 == etrike::protocol::CodecStatus::Ok, "Encode 0x169 SES command succeeds");
    TEST_CHECK_EQ(frame_169.id, 0x169u, "0x169 CAN ID is VCU_SES_REQ");
    TEST_CHECK_EQ(frame_169.dlc, 8u, "0x169 DLC is 8");

    // Verify SES XOR-8 checksum in byte 7
    uint8_t expected_cs = 0;
    for (size_t i = 0; i < 7; ++i) expected_cs ^= frame_169.data[i];
    expected_cs ^= 0xFF;
    TEST_CHECK_EQ(frame_169.data[7], expected_cs, "SES 0x169 byte 7 contains valid XOR-8 checksum");

    // 3. RT Drive Command (0x204) to MTR
    generated::RtDriveCmd rt_drive{};
    rt_drive.motor_speed_mmps = decoded_host_cmd.speed_mmps;
    rt_drive.gear = decoded_host_cmd.gear;
    etrike::protocol::Frame frame_204{};
    generated::encode(rt_drive, frame_204);

    TEST_CHECK_EQ(frame_204.id, 0x204u, "0x204 CAN ID is RT_DRIVE_CMD");
    TEST_CHECK_EQ(frame_204.dlc, 5u, "0x204 DLC is 5");

    generated::RtDriveCmd decoded_204{};
    generated::decode(frame_204.view(), decoded_204);
    TEST_CHECK_EQ(decoded_204.motor_speed_mmps, 2200, "MTR receives 2200 mm/s setpoint");
    TEST_CHECK_EQ(decoded_204.gear, 1, "MTR receives Gear 1 (Drive)");

    // 4. Host Brake Request (0x301) to SEB Brake (0x7B9)
    generated::HostBrakeReq host_brake{};
    host_brake.brake_pressure_kpa = 2500; // 2500 kPa
    etrike::protocol::Frame frame_301{};
    generated::encode(host_brake, frame_301);
    TEST_CHECK_EQ(frame_301.id, 0x301u, "0x301 CAN ID is HOST_BRAKE_REQ");

    codecs::seb::Command seb_cmd{};
    seb_cmd.control_enable = true;
    seb_cmd.control_mode = codecs::seb::ControlMode::Pressure;
    seb_cmd.pressure_request_raw = static_cast<uint8_t>(host_brake.brake_pressure_kpa / 50); // 50 kPa/LSB = 50
    seb_cmd.rolling_counter = 2;
    etrike::protocol::Frame frame_7b9{};
    auto status_7b9 = codecs::seb::encode_command(seb_cmd, frame_7b9);
    TEST_CHECK(status_7b9 == etrike::protocol::CodecStatus::Ok, "Encode 0x7B9 SEB command succeeds");
    TEST_CHECK_EQ(frame_7b9.id, 0x7B9u, "0x7B9 CAN ID is VCU_SEB_REQ");
}

// ============================================================================
// Test Suite 2: Flow B — Remote Control Mode (FlySky FS-i6 -> RM -> CAN)
// ============================================================================
static void test_flow_b_remote_control_pipeline() {
    std::printf("\n--- Flow B: Remote Control Mode Pipeline ---\n");

    uint32_t raw_us[rm::kNumRcChannels] = {1500, 1500, 1500, 1500, 1000, 1500};
    uint32_t last_edge_ms[rm::kNumRcChannels] = {100, 100, 100, 100, 100, 100};
    uint32_t now_ms = 110;

    // 1. Test FS-i6 SWC 3-Position Gear Switch & SWB Ignition
    // Reverse: pulse ~1000 µs (threshold <= kGearRevMaxUs 1250)
    raw_us[5] = 1000;
    raw_us[4] = 2000; // SWB Down = ON (>= 1600)
    auto state_rev = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    TEST_CHECK_EQ(static_cast<int>(state_rev.gear), static_cast<int>(can::Gear::R),
                  "SWC at 1000 µs decodes to Reverse Gear");
    TEST_CHECK_EQ(state_rev.ignition, true, "SWB at 2000 µs activates Ignition");

    // Neutral: pulse ~1500 µs
    raw_us[5] = 1500;
    auto state_neut = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    TEST_CHECK_EQ(static_cast<int>(state_neut.gear), static_cast<int>(can::Gear::N),
                  "SWC at 1500 µs decodes to Neutral/Park");

    // Drive: pulse ~2000 µs (>= kGearDriveMinUs 1750)
    raw_us[5] = 2000;
    raw_us[0] = 1750; // Right steer
    raw_us[1] = 1800; // Brake trigger
    raw_us[2] = 1800; // 80% throttle (Left Stick Vertical, CH3)
    auto state_drive = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    TEST_CHECK_EQ(static_cast<int>(state_drive.gear), static_cast<int>(can::Gear::D),
                  "SWC at 2000 µs decodes to Drive Gear");
    TEST_CHECK(state_drive.steering_deg > 0.0f, "Right stick produces positive steering angle");
    TEST_CHECK(state_drive.brake_stroke_mm > 0.0f, "Brake trigger produces positive brake stroke");
    TEST_CHECK(state_drive.throttle_norm > 0.7f, "Left stick vertical decodes to throttle_norm > 0.7");

    // 2. Verify CAN transmission encoding
    generated::HmiModeReq hmi_mode{};
    hmi_mode.req_mode = generated::HmiModeReq::kReqModeManual;
    hmi_mode.rolling_counter = 1;
    etrike::protocol::Frame frame_111{};
    generated::encode(hmi_mode, frame_111);
    TEST_CHECK_EQ(frame_111.id, 0x111u, "0x111 CAN ID matches HMI_MODE_REQ");
    TEST_CHECK_EQ(frame_111.dlc, 2u, "0x111 DLC is 2");

    generated::HmiPwrReq hmi_pwr{};
    hmi_pwr.req_start = state_drive.ignition ? 1 : 0;
    hmi_pwr.rolling_counter = 1;
    etrike::protocol::Frame frame_112{};
    generated::encode(hmi_pwr, frame_112);
    TEST_CHECK_EQ(frame_112.id, 0x112u, "0x112 CAN ID matches HMI_PWR_REQ");
    TEST_CHECK_EQ(frame_112.dlc, 2u, "0x112 DLC is 2");
}

// ============================================================================
// Test Suite 3: Flow E — RM Standalone Direct Actuator Bypass on Low CAN
// (Host, RT, and SYS Disconnected or Powered OFF)
// ============================================================================
static void test_flow_e_rm_standalone_bypass_pipeline() {
    std::printf("\n--- Flow E: RM Standalone Direct Bypass Pipeline (RM -> SES, SEB, MTR) ---\n");

    // RM directly commands SES, SEB, and MTR on Low CAN bus
    // Set RC inputs: Drive engaged, SWB ignition ON, 50% speed trim, right steer 15.0 deg, 5.0 mm brake
    uint32_t raw_us[rm::kNumRcChannels] = {1650, 1600, 1500, 1500, 2000, 2000};
    uint32_t last_edge_ms[rm::kNumRcChannels] = {100, 100, 100, 100, 100, 100};
    uint32_t now_ms = 110;

    auto snap = rm::decode_rc_signals(raw_us, last_edge_ms, now_ms);
    TEST_CHECK(snap.signal_valid, "RM RC signals valid");
    TEST_CHECK(snap.ignition, "RM Ignition ON");
    TEST_CHECK_EQ(static_cast<int>(snap.gear), static_cast<int>(can::Gear::D), "RM Gear is Drive");

    // 1. Direct Steering Setpoint -> 0x169 VCU_SES_REQ
    bool drive_active = snap.signal_valid && snap.ignition &&
                       (snap.gear == can::Gear::D || snap.gear == can::Gear::R);
    TEST_CHECK(drive_active, "RM Drive active for steering & traction");

    codecs::ses::Command ses_cmd{};
    ses_cmd.alignment_enable = snap.signal_valid && snap.ignition;
    ses_cmd.control_enable = drive_active;
    ses_cmd.target_angle_raw = static_cast<int16_t>(snap.steering_deg * 10.0f);
    ses_cmd.target_speed_raw = 328;
    ses_cmd.rolling_counter = 1;
    etrike::protocol::Frame ses_fr{};
    auto status_ses = codecs::ses::encode_command(ses_cmd, ses_fr);
    TEST_CHECK(status_ses == etrike::protocol::CodecStatus::Ok, "RM encodes 0x169 VCU_SES_REQ directly");
    TEST_CHECK_EQ(ses_fr.id, 0x169u, "SES frame ID is 0x169");
    TEST_CHECK(ses_cmd.target_angle_raw > 0, "Direct steer angle commanded positive");

    // 2. Direct Brake Setpoint -> 0x7B9 VCU_SEB_REQ
    codecs::seb::Command seb_cmd{};
    seb_cmd.alignment_enable = snap.signal_valid;
    seb_cmd.control_enable = snap.signal_valid;
    seb_cmd.control_mode = codecs::seb::ControlMode::Stroke;
    float commanded_stroke = snap.signal_valid ? snap.brake_stroke_mm : rm::kMaxBrakeStrokeMm;
    uint16_t stroke_raw = static_cast<uint16_t>((commanded_stroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
    seb_cmd.stroke_request_raw = stroke_raw;
    seb_cmd.rolling_counter = 1;
    etrike::protocol::Frame seb_fr{};
    auto status_seb = codecs::seb::encode_command(seb_cmd, seb_fr);
    TEST_CHECK(status_seb == etrike::protocol::CodecStatus::Ok, "RM encodes 0x7B9 VCU_SEB_REQ directly");
    TEST_CHECK_EQ(seb_fr.id, 0x7B9u, "SEB frame ID is 0x7B9");

    // 3. Direct Canonical Motor Command -> 0x204 RT_DRIVE_CMD
    int32_t target_motor_speed = static_cast<int32_t>(snap.throttle_norm * shared::kMaxSpeedFwdMmps);
    generated::RtDriveCmd drive_cmd{};
    drive_cmd.motor_speed_mmps = target_motor_speed;
    drive_cmd.gear = static_cast<uint8_t>(snap.gear);
    etrike::protocol::Frame drive_fr{};
    generated::encode(drive_cmd, drive_fr);
    TEST_CHECK_EQ(drive_fr.id, 0x204u, "RM drive frame ID is 0x204");
    TEST_CHECK(drive_cmd.motor_speed_mmps > 0, "RM motor speed setpoint > 0");

    // Feed RM's direct 0x204 frame directly into MTR STM32 (without RT or SYS)
    mtr::RelayController relays_direct{};
    mtr::DacController dac_direct{};
    mtr::MotorManager mtr_direct{relays_direct, dac_direct};
    mtr_direct.init();

    // Send power + mode authority to MTR (0x113 SYS_PWR_CMD + 0x110 SYS_MODE_CMD).
    // MTR no longer takes 0x112; RM emulates SYS on the bench.
    auth_power(mtr_direct, true, 90);
    auth_mode(mtr_direct, can::Mode::Auto, 90);

    can::Frame can_204_in{};
    can_204_in.id = drive_fr.id;
    can_204_in.dlc = drive_fr.dlc;
    std::memcpy(can_204_in.data.data(), drive_fr.data.data(), drive_fr.dlc);

    mtr_direct.handle_frame(can_204_in, 100);
    mtr_direct.tick(100);

    TEST_CHECK_EQ(relays_direct.state(), mtr::RelayController::State::Drive,
                  "MTR switches to Drive relay directly from RM 0x204");
    TEST_CHECK(dac_direct.current_code() > 0,
               "MTR DAC outputs throttle voltage directly from RM 0x204");

    // 4. RC Signal Loss Deadman Guard -> 0x001 SAFETY_ESTOP from RM
    raw_us[0] = 0; // Signal lost on steering
    auto snap_lost = rm::decode_rc_signals(raw_us, last_edge_ms, 250);
    TEST_CHECK(!snap_lost.signal_valid, "RC signal correctly marked invalid");
    TEST_CHECK_EQ(snap_lost.brake_stroke_mm, rm::kMaxBrakeStrokeMm, "Emergency brake maxed on signal loss");

    can::Frame rm_estop{};
    rm_estop.id = 0x001;
    rm_estop.dlc = 0; // Wire contract

    mtr_direct.handle_frame(rm_estop, 300);
    mtr_direct.tick(300);

    TEST_CHECK_EQ(relays_direct.state(), mtr::RelayController::State::Off,
                  "MTR immediately cuts relays upon RM fail-safe ESTOP");
    TEST_CHECK_EQ(dac_direct.current_code(), 0,
                  "MTR DAC forced to 0V upon RM fail-safe ESTOP");
}

// ============================================================================
// Test Suite 4: Flow C — Motor Actuation & Relay Control (MTR Node in Hierarchy)
// ============================================================================
static void test_flow_c_motor_actuation_pipeline() {
    std::printf("\n--- Flow C: Motor Actuation & Relay Pipeline ---\n");

    mtr::RelayController relays{};
    mtr::DacController dac{};
    mtr::MotorManager motor{relays, dac};
    motor.init();

    // 1. Initial State: Neutral/Park (relays OFF before ignition), Speed 0
    motor.tick(0);
    TEST_CHECK_EQ(motor.target_speed_mmps(), 0, "Motor initial target speed is 0");
    TEST_CHECK_EQ(dac.current_code(), 0, "MCP4725 DAC initial code is 0 (0.0 V)");
    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Off, "Relay state initially Off before ignition");

    // Power-on authority via 0x113 SYS_PWR_CMD (MTR no longer takes 0x112).
    auth_power(motor, true, 10);
    motor.tick(10);
    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Park, "Relay state transitions to Park upon ignition ON");

    // 2. Command Forward Drive 1500 mm/s in AUTO mode (mode authority 0x110)
    auth_mode(motor, can::Mode::Auto, 50);

    can::Frame fwd_frame{};
    fwd_frame.id = can::kIdRtDriveCmd; // 0x204
    fwd_frame.dlc = 5;
    can::gen::RtDriveCmd fwd_cmd{};
    fwd_cmd.motor_speed_mmps = 1500;
    fwd_cmd.gear = static_cast<uint8_t>(can::Gear::D);
    can::gen::encode_rt_drive_cmd(fwd_cmd, fwd_frame);

    motor.handle_frame(fwd_frame, 100);
    motor.tick(100);

    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Drive, "Drive relay PA2 is energized");
    TEST_CHECK_EQ(motor.target_speed_mmps(), 1500, "Motor target speed is 1500 mm/s");
    TEST_CHECK(dac.current_code() > 0, "DAC code is scaled to active throttle range (> 0)");

    // 3. Direction Change (Drive -> Reverse)
    can::Frame rev_frame{};
    rev_frame.id = can::kIdRtDriveCmd;
    rev_frame.dlc = 5;
    can::gen::RtDriveCmd rev_cmd{};
    rev_cmd.motor_speed_mmps = -500;
    rev_cmd.gear = static_cast<uint8_t>(can::Gear::R);
    can::gen::encode_rt_drive_cmd(rev_cmd, rev_frame);

    motor.handle_frame(rev_frame, 500);
    motor.tick(500); // 50 ms dwell period initiated, relays transition to Park, DAC forced to 0
    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Park, "Relays safely hold Park during 50ms dwell");
    TEST_CHECK_EQ(dac.current_code(), 0, "DAC forced to 0 during dwell");

    // Advance 60 ms past dwell completion
    motor.tick(560);

    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Reverse, "Reverse relay PA0 energized");
    TEST_CHECK_EQ(motor.target_speed_mmps(), -500, "Target speed is now -500 mm/s reverse");
    TEST_CHECK(dac.current_code() > 0, "DAC code active in reverse");

    // 4. Verify MTR Feedback Frame (0x206)
    can::Frame frame_206 = motor.build_motor_feedback_frame();
    TEST_CHECK_EQ(frame_206.id, 0x206u, "0x206 CAN ID matches MTR_MOTOR_FBK");
    TEST_CHECK_EQ(frame_206.dlc, 4u, "0x206 DLC is 4");
}

// ============================================================================
// Test Suite 4: Flow D — Global Emergency Stop (ESTOP)
// ============================================================================
static void test_flow_d_estop_pipeline() {
    std::printf("\n--- Flow D: Global Emergency Stop Pipeline ---\n");

    mtr::RelayController relays{};
    mtr::DacController dac{};
    mtr::MotorManager motor{relays, dac};
    motor.init();

    // Turn on Ignition via authority frames 0x113 (power) + 0x110 (mode).
    auth_power(motor, true, 50);
    auth_mode(motor, can::Mode::Auto, 50);

    // Spin up motor in Drive mode
    can::Frame run_frame{};
    run_frame.id = can::kIdRtDriveCmd;
    run_frame.dlc = 5;
    can::gen::RtDriveCmd run_cmd{};
    run_cmd.motor_speed_mmps = 2000;
    run_cmd.gear = static_cast<uint8_t>(can::Gear::D);
    can::gen::encode_rt_drive_cmd(run_cmd, run_frame);

    motor.handle_frame(run_frame, 100);
    motor.tick(100);

    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Drive, "Drive engaged prior to ESTOP");
    TEST_CHECK(dac.current_code() > 0, "Throttle active prior to ESTOP");

    // Inject 0x001 SAFETY_ESTOP (DLC 0 wire contract)
    can::Frame estop_frame{};
    estop_frame.id = 0x001;
    estop_frame.dlc = 0; // Strict zero-byte contract
    TEST_CHECK_EQ(estop_frame.dlc, 0, "0x001 wire contract has DLC = 0");

    motor.handle_frame(estop_frame, 150);
    motor.tick(150);

    TEST_CHECK_EQ(relays.state(), mtr::RelayController::State::Off, "All relays de-energized on ESTOP");
    TEST_CHECK_EQ(dac.current_code(), 0, "DAC code forced to 0 (0.0 V) on ESTOP");
    TEST_CHECK_EQ(motor.target_speed_mmps(), 0, "Target speed clamped to 0");

    // Verify Gap #15 redundant acknowledgment bit on 0x206
    can::Frame fbk_frame = motor.build_motor_feedback_frame();
    can::gen::MtrMotorFbk fbk{};
    can::gen::decode_mtr_motor_fbk(fbk_frame.view(), fbk);
    TEST_CHECK((fbk.fault_flags & shared::kMtrFaultEstopActive) != 0,
               "Fault flag bit 0 (kMtrFaultEstopActive) asserted in 0x206 feedback");
}

// ============================================================================
// Test Suite 5: CAN Gateway Forwarding & Message Matrix Validation
// ============================================================================
static void test_gateway_message_matrix() {
    std::printf("\n--- Message Matrix & DLC Consistency Checks ---\n");

    // Check all canonical message DLCs per documentation
    TEST_CHECK_EQ(generated::SafetyEstop::kDlc, 0, "0x001 SAFETY_ESTOP DLC = 0");
    TEST_CHECK_EQ(generated::SysSafetySts::kDlc, 3, "0x011 SYS_SAFETY_STATUS DLC = 3");
    TEST_CHECK_EQ(generated::PwtDcdcCmd::kDlc, 8, "0x10262B27 PWT_DCDC_CMD DLC = 8");
    TEST_CHECK_EQ(generated::SysModeCmd::kDlc, 2, "0x110 SYS_MODE_CMD DLC = 2");
    TEST_CHECK_EQ(generated::HmiModeReq::kDlc, 2, "0x111 HMI_MODE_REQ DLC = 2");
    TEST_CHECK_EQ(generated::HmiPwrReq::kDlc, 2, "0x112 HMI_PWR_REQ DLC = 2");
    TEST_CHECK_EQ(generated::SysPwrCmd::kDlc, 2, "0x113 SYS_PWR_CMD DLC = 2");
    TEST_CHECK_EQ(generated::SysThrottleSts::kDlc, 2, "0x120 SYS_THROTTLE_STS DLC = 2");
    TEST_CHECK_EQ(codecs::ses::kDlc, 8, "0x169 VCU_SES_REQ DLC = 8");
    TEST_CHECK_EQ(generated::RtDriveCmd::kDlc, 5, "0x204 RT_DRIVE_CMD DLC = 5");
    TEST_CHECK_EQ(generated::RtBrakeCmd::kDlc, 4, "0x205 RT_BRAKE_CMD DLC = 4");
    TEST_CHECK_EQ(generated::MtrMotorFbk::kDlc, 4, "0x206 MTR_MOTOR_FBK DLC = 4");
    TEST_CHECK_EQ(generated::RtStateRpt::kDlc, 6, "0x210 RT_STATE_REPORT DLC = 6");
    TEST_CHECK_EQ(generated::RtPidRpt::kDlc, 6, "0x220 RT_PID_RPT DLC = 6");
    TEST_CHECK_EQ(generated::HostDriveCmd::kDlc, 8, "0x300 HOST_DRIVE_CMD DLC = 8");
    TEST_CHECK_EQ(generated::HostBrakeReq::kDlc, 4, "0x301 HOST_BRAKE_REQ DLC = 4");
    TEST_CHECK_EQ(generated::HostLightCmd::kDlc, 1, "0x302 HOST_LIGHT_CMD DLC = 1");
    TEST_CHECK_EQ(generated::HostObstacleDist::kDlc, 4, "0x400 RT_OBSTACLE_DIST DLC = 4");
    TEST_CHECK_EQ(codecs::seb::kDlc, 8, "0x7B9 VCU_SEB_REQ DLC = 8");
    TEST_CHECK_EQ(generated::HostHeartbeat::kDlc, 2, "0x7FC HOST_HEARTBEAT DLC = 2");
    TEST_CHECK_EQ(generated::RtHeartbeat::kDlc, 2, "0x7FD RT_HEARTBEAT DLC = 2");
    TEST_CHECK_EQ(generated::SysHeartbeat::kDlc, 2, "0x7FE SYS_HEARTBEAT DLC = 2");
}

int main() {
    std::printf("====================================================\n");
    std::printf("  E-Trike End-to-End Architecture & Data Flow Tests \n");
    std::printf("====================================================\n");

    test_flow_a_autonomous_pipeline();
    test_flow_b_remote_control_pipeline();
    test_flow_e_rm_standalone_bypass_pipeline();
    test_flow_c_motor_actuation_pipeline();
    test_flow_d_estop_pipeline();
    test_gateway_message_matrix();

    int total = g_pass + g_fail;
    std::printf("\n====================================================\n");
    std::printf("Result: %d Passed, %d Failed (Total %d)\n", g_pass, g_fail, total);
    std::printf("====================================================\n");

    return (g_fail == 0) ? 0 : 1;
}
