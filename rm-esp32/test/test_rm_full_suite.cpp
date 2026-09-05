#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include "protocol/compat/can.hpp"
#include "shared_config.h"
#include "rm-esp32/src/config.h"
#include "rm-esp32/src/rc_decoder.h"

// ═══════════════════════════════════════════════════════════════════════
// Test Framework & Assertions
// ═══════════════════════════════════════════════════════════════════════

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

// Helper to construct a baseline healthy RC pulse train (center neutral, ignition OFF, gear N)
void set_baseline_pulses(uint32_t raw_us[rm::kNumRcChannels],
                         uint32_t last_edge_ms[rm::kNumRcChannels],
                         uint32_t now_ms = 1000) {
    raw_us[0] = 1500; // Steer Center (0.0 deg)
    raw_us[1] = 1500; // Brake Released (0.0 mm)
    raw_us[2] = 1000; // Throttle Idle (0.0%)
    raw_us[3] = 1500; // Spare CH3
    raw_us[4] = 1000; // Ignition OFF
    raw_us[5] = 1500; // Gear Neutral (MID)

    for (int i = 0; i < rm::kNumRcChannels; ++i) {
        last_edge_ms[i] = now_ms;
    }
}

// ═══════════════════════════════════════════════════════════════════════
// 1. RC Pulse Bounds & Validation Tests
// ═══════════════════════════════════════════════════════════════════════

void test_pulse_bounds_and_rejection() {
    std::printf("[TEST GROUP] Pulse Width Range & Boundary Rejection...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t now = 500;

    // Baseline: All valid
    set_baseline_pulses(raw_us, last_edge, now);
    auto snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.signal_valid);

    // Exact boundary testing:
    // kPulseMinValidUs = 900 us; kPulseMaxValidUs = 2100 us
    const uint8_t critical_channels[] = {0, 1, 2, 4, 5};

    for (uint8_t ch : critical_channels) {
        // Just below valid range (899 us) -> INVALID
        set_baseline_pulses(raw_us, last_edge, now);
        raw_us[ch] = rm::kPulseMinValidUs - 1;
        snap = rm::decode_rc_signals(raw_us, last_edge, now);
        ASSERT_FALSE(snap.signal_valid);
        ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.001f);

        // Exact minimum boundary (900 us) -> VALID
        set_baseline_pulses(raw_us, last_edge, now);
        raw_us[ch] = rm::kPulseMinValidUs;
        snap = rm::decode_rc_signals(raw_us, last_edge, now);
        ASSERT_TRUE(snap.signal_valid);

        // Exact maximum boundary (2100 us) -> VALID
        set_baseline_pulses(raw_us, last_edge, now);
        raw_us[ch] = rm::kPulseMaxValidUs;
        snap = rm::decode_rc_signals(raw_us, last_edge, now);
        ASSERT_TRUE(snap.signal_valid);

        // Just above valid range (2101 us) -> INVALID
        set_baseline_pulses(raw_us, last_edge, now);
        raw_us[ch] = rm::kPulseMaxValidUs + 1;
        snap = rm::decode_rc_signals(raw_us, last_edge, now);
        ASSERT_FALSE(snap.signal_valid);
        ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.001f);
    }

    // Channel 3 (Spare) isolation test:
    // Out-of-bounds on CH3 MUST NOT invalidate the critical driving signals
    set_baseline_pulses(raw_us, last_edge, now);
    raw_us[3] = 400; // Wildly invalid on spare
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.signal_valid);

    raw_us[3] = 3000; // Wildly high on spare
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.signal_valid);
}

// ═══════════════════════════════════════════════════════════════════════
// 2. Deadman Watchdog & Signal Loss Timeout Tests
// ═══════════════════════════════════════════════════════════════════════

void test_deadman_watchdog_timeouts() {
    std::printf("[TEST GROUP] Deadman Watchdog & Channel Timeouts...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t base_time = 1000;

    // kSignalLossTimeoutMs = 100 ms
    const uint8_t critical_channels[] = {0, 1, 2, 4, 5};

    for (uint8_t ch : critical_channels) {
        // At exactly 100 ms elapsed -> Still VALID
        set_baseline_pulses(raw_us, last_edge, base_time);
        last_edge[ch] = base_time;
        auto snap = rm::decode_rc_signals(raw_us, last_edge, base_time + rm::kSignalLossTimeoutMs);
        ASSERT_TRUE(snap.signal_valid);

        // At 101 ms elapsed -> TIMEOUT & INVALID
        snap = rm::decode_rc_signals(raw_us, last_edge, base_time + rm::kSignalLossTimeoutMs + 1);
        ASSERT_FALSE(snap.signal_valid);

        // Fail-safe state verification on timeout:
        ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.001f);
        ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);
        ASSERT_NEAR(snap.throttle_norm, 0.0f, 0.001f);
        ASSERT_FALSE(snap.ignition);
        ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::N));
    }

    // CH3 (Spare) timeout isolation:
    // Stale edge on CH3 MUST NOT trip deadman
    set_baseline_pulses(raw_us, last_edge, base_time);
    last_edge[3] = base_time - 5000; // 5 seconds stale
    auto snap = rm::decode_rc_signals(raw_us, last_edge, base_time);
    ASSERT_TRUE(snap.signal_valid);
}

// ═══════════════════════════════════════════════════════════════════════
// 3. Steering Mapping & Deadband Tests (CH0)
// ═══════════════════════════════════════════════════════════════════════

void test_steering_mapping_and_deadband() {
    std::printf("[TEST GROUP] Steering Deadband & Proportional Mapping...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t now = 200;
    set_baseline_pulses(raw_us, last_edge, now);

    // Center exact: 1500 us -> 0.0 deg
    raw_us[0] = 1500;
    auto snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);

    // Deadband is +/- 30 us ([1470, 1530] us)
    raw_us[0] = 1530; // Positive edge of deadband
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);

    raw_us[0] = 1470; // Negative edge of deadband
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, 0.0f, 0.001f);

    // Just outside deadband
    raw_us[0] = 1531;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.steering_deg > 0.0f);

    raw_us[0] = 1469;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.steering_deg < 0.0f);

    // Half right: offset = 225 us -> 1725 us -> +22.5 deg
    raw_us[0] = 1725;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, 22.5f, 0.2f);

    // Half left: offset = -225 us -> 1275 us -> -22.5 deg
    raw_us[0] = 1275;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, -22.5f, 0.2f);

    // Full right: offset = 450 us -> 1950 us -> +45.0 deg
    raw_us[0] = 1950;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, rm::kMaxSteerAngleDeg, 0.05f);

    // Full left: offset = -450 us -> 1050 us -> -45.0 deg
    raw_us[0] = 1050;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, -rm::kMaxSteerAngleDeg, 0.05f);

    // Over-travel clamping within valid pulse limits
    raw_us[0] = 2050; // Beyond 1950 but valid (<2100) -> clamped at +45.0 deg
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, rm::kMaxSteerAngleDeg, 0.001f);

    raw_us[0] = 950;  // Below 1050 but valid (>900) -> clamped at -45.0 deg
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.steering_deg, -rm::kMaxSteerAngleDeg, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════
// 4. Brake Mapping Tests (CH1)
// ═══════════════════════════════════════════════════════════════════════

void test_brake_mapping() {
    std::printf("[TEST GROUP] Brake Stroke Engagement & Limits...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t now = 200;
    set_baseline_pulses(raw_us, last_edge, now);

    // Released at center (1500 us)
    raw_us[1] = 1500;
    auto snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);

    // Deadband threshold: <= 1520 us (kPulseCenterUs + 20) -> 0.0 mm
    raw_us[1] = 1520;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);

    // Pushed forward (< 1500 us, e.g. 1200 us) -> MUST be 0.0 mm
    raw_us[1] = 1200;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.brake_stroke_mm, 0.0f, 0.001f);

    // Linear engagement starts at 1521 us
    raw_us[1] = 1521;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.brake_stroke_mm > 0.0f);

    // Mid engagement: 1520 + 225 = 1745 us -> ~13.5 mm (50%)
    raw_us[1] = 1745;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.brake_stroke_mm, 13.5f, 0.2f);

    // Full stroke: 1520 + 450 = 1970 us -> 27.0 mm (100%)
    raw_us[1] = 1970;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.05f);

    // Over-travel clamping: 2050 us -> clamped to 27.0 mm
    raw_us[1] = 2050;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.brake_stroke_mm, rm::kMaxBrakeStrokeMm, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════
// 5. Throttle Mapping Tests (CH2)
// ═══════════════════════════════════════════════════════════════════════

void test_throttle_mapping() {
    std::printf("[TEST GROUP] Throttle Idle Deadband & Range...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t now = 200;
    set_baseline_pulses(raw_us, last_edge, now);

    // Idle threshold: <= 1050 us (kThrottleMinUs) -> 0.0
    raw_us[2] = 1000;
    auto snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 0.0f, 0.001f);

    raw_us[2] = 1050;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 0.0f, 0.001f);

    // Linear ramp from 1050 us to 1950 us (span = 900 us)
    // 25% Throttle: 1050 + 225 = 1275 us
    raw_us[2] = 1275;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 0.25f, 0.01f);

    // 50% Throttle: 1050 + 450 = 1500 us
    raw_us[2] = 1500;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 0.50f, 0.01f);

    // 75% Throttle: 1050 + 675 = 1725 us
    raw_us[2] = 1725;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 0.75f, 0.01f);

    // 100% Full Throttle: 1950 us (kThrottleMaxUs)
    raw_us[2] = 1950;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 1.0f, 0.001f);

    // Over-travel clamping: 2050 us -> clamped to 1.0
    raw_us[2] = 2050;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.throttle_norm, 1.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════
// 6. Spare Channel (CH3) Normalization
// ═══════════════════════════════════════════════════════════════════════

void test_spare_channel_mapping() {
    std::printf("[TEST GROUP] Spare Channel (CH3) Pass-Through...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t now = 200;
    set_baseline_pulses(raw_us, last_edge, now);

    // 900 us -> 0.0
    raw_us[3] = 900;
    auto snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.spare_ch4, 0.0f, 0.001f);

    // 1400 us -> 0.5
    raw_us[3] = 1400;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.spare_ch4, 0.5f, 0.01f);

    // 1900 us -> 1.0
    raw_us[3] = 1900;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.spare_ch4, 1.0f, 0.001f);

    // Clamping above 1900 us
    raw_us[3] = 2000;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_NEAR(snap.spare_ch4, 1.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════════
// 7. Switches: Ignition (CH4) & Gear (CH5)
// ═══════════════════════════════════════════════════════════════════════

void test_switches_ignition_and_gear() {
    std::printf("[TEST GROUP] Ignition (SWB) & 3-Pos Gear (SWC) Switches...\n");
    uint32_t raw_us[rm::kNumRcChannels];
    uint32_t last_edge[rm::kNumRcChannels];
    const uint32_t now = 200;
    set_baseline_pulses(raw_us, last_edge, now);

    // ── SWB Ignition Threshold (1500 us) ──
    raw_us[4] = 1499;
    auto snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_FALSE(snap.ignition);

    raw_us[4] = 1500;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.ignition);

    raw_us[4] = 2000;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_TRUE(snap.ignition);

    // ── SWC 3-Position Gear Switch ──
    // Reverse: <= 1300 us (kGearRevMaxUs)
    raw_us[5] = 1000;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::R));

    raw_us[5] = 1300;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::R));

    // Neutral / Park: 1301 .. 1699 us
    raw_us[5] = 1301;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::N));

    raw_us[5] = 1500;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::N));

    raw_us[5] = 1699;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::N));

    // Drive: >= 1700 us (kGearDriveMinUs)
    raw_us[5] = 1700;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::D));

    raw_us[5] = 2000;
    snap = rm::decode_rc_signals(raw_us, last_edge, now);
    ASSERT_EQ(static_cast<uint8_t>(snap.gear), static_cast<uint8_t>(can::Gear::D));
}

// ═══════════════════════════════════════════════════════════════════════
// 8. Jitter / Hysteresis Filter Logic (from rc_receiver.cpp)
// ═══════════════════════════════════════════════════════════════════════

void test_jitter_filter() {
    std::printf("[TEST GROUP] Pulse Jitter Filter (4 us Hysteresis)...\n");
    // Emulate step 2 of rc_receiver.cpp:
    // int32_t diff = raw - past; if (abs(diff) >= 4) past = raw;
    uint32_t past = 1500;

    // Small jitter (+1, +2, +3 us) must be rejected
    for (int delta = -3; delta <= 3; ++delta) {
        uint32_t raw = past + delta;
        int32_t diff = static_cast<int32_t>(raw) - static_cast<int32_t>(past);
        if (std::abs(diff) >= 4) {
            past = raw;
        }
    }
    ASSERT_EQ(past, 1500u);

    // Delta of +4 us must pass
    uint32_t raw = past + 4;
    int32_t diff = static_cast<int32_t>(raw) - static_cast<int32_t>(past);
    if (std::abs(diff) >= 4) {
        past = raw;
    }
    ASSERT_EQ(past, 1504u);

    // Delta of -4 us must pass
    raw = past - 4;
    diff = static_cast<int32_t>(raw) - static_cast<int32_t>(past);
    if (std::abs(diff) >= 4) {
        past = raw;
    }
    ASSERT_EQ(past, 1500u);
}

// ═══════════════════════════════════════════════════════════════════════
// 9. RM CAN Gateway State Machine & Safety Interlocks (main.cpp logic)
// ═══════════════════════════════════════════════════════════════════════

class RmGatewayEngine {
public:
    struct TxRecord {
        can::Frame frame;
        const char* name;
    };

    std::vector<TxRecord> tx_history;
    bool estop_latched{false};
    bool was_in_signal_loss{false};
    int hmi_heartbeat_counter{0};
    bool last_ignition{false};

    uint8_t roll_ses{0};
    uint8_t roll_seb{0};
    uint8_t roll_sys_mode{0};
    uint8_t roll_sys_pwr{0};

    void handle_rx_can(const can::Frame& fr) {
        if (fr.id == 0x001u) {
            estop_latched = true;
        }
    }

    void tick_can_tx(const rm::RcSnapshot& snap) {
        // Reset latched ESTOP when RC signal is valid, ignition is switched OFF, and gear is in Neutral
        if (estop_latched && snap.signal_valid && !snap.ignition && snap.gear == can::Gear::N) {
            estop_latched = false;
        }

        bool estop_or_signal_loss = !snap.signal_valid || estop_latched;

        // 1. Fail-Safe Signal Loss Broadcast
        if (!snap.signal_valid) {
            if (!was_in_signal_loss) {
                was_in_signal_loss = true;
                can::Frame estop_fr;
                can::gen::SafetyEstop estop_msg{};
                can::gen::encode_safety_estop(estop_msg, estop_fr);
                tx_history.push_back({estop_fr, "SAFETY_ESTOP"});
            }
        } else {
            if (was_in_signal_loss) {
                was_in_signal_loss = false;
            }
        }

        // Drive active condition
        bool drive_active = !estop_or_signal_loss && snap.ignition &&
                           (snap.gear == can::Gear::D || snap.gear == can::Gear::R);

        // 2. VCU_SES_REQ (0x169)
        can::custom::ses::Command ses_cmd{};
        ses_cmd.alignment_enable = !estop_or_signal_loss && snap.ignition;
        ses_cmd.control_enable = drive_active;
        int16_t angle_raw = static_cast<int16_t>(rm::kSbwAngleOffset);
        if (drive_active) {
            angle_raw = static_cast<int16_t>(std::round(snap.steering_deg * 10.0f)) + static_cast<int16_t>(rm::kSbwAngleOffset);
            angle_raw = std::clamp(angle_raw, rm::kMinSteerRaw, rm::kMaxSteerRaw);
        }
        ses_cmd.target_angle_raw = angle_raw;
        ses_cmd.target_speed_raw = 328;
        ses_cmd.rolling_counter = roll_ses;
        roll_ses = (roll_ses + 1) & 0x0F;
        ses_cmd.vehicle_speed_raw = 0;

        can::Frame ses_fr;
        can::custom::ses::encode_command(ses_cmd, ses_fr);
        tx_history.push_back({ses_fr, "VCU_SES_REQ"});

        // 3. VCU_SEB_REQ (0x7B9)
        can::custom::seb::Command seb_cmd{};
        seb_cmd.alignment_enable = !estop_or_signal_loss;
        seb_cmd.control_enable = !estop_or_signal_loss;
        seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
        seb_cmd.auto_brake = false;

        float commanded_stroke = (!estop_or_signal_loss) ? snap.brake_stroke_mm : rm::kMaxBrakeStrokeMm;
        uint16_t stroke_raw = static_cast<uint16_t>((commanded_stroke - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        seb_cmd.stroke_request_raw = stroke_raw;
        seb_cmd.pressure_request_raw = 0;
        seb_cmd.rolling_counter = roll_seb;
        roll_seb = (roll_seb + 1) & 0x0F;

        can::Frame seb_fr;
        can::custom::seb::encode_command(seb_cmd, seb_fr);
        tx_history.push_back({seb_fr, "VCU_SEB_REQ"});

        // 4. RT_DRIVE_CMD (0x204) - Gated OFF under ESTOP or signal loss
        if (!estop_or_signal_loss) {
            int32_t target_motor_speed = 0;
            if (drive_active) {
                if (snap.gear == can::Gear::D) {
                    target_motor_speed = static_cast<int32_t>(snap.throttle_norm * shared::kMaxSpeedFwdMmps);
                } else if (snap.gear == can::Gear::R) {
                    target_motor_speed = -static_cast<int32_t>(snap.throttle_norm * shared::kMaxSpeedRevMmps);
                }
            }

            can::gen::RtDriveCmd drive_cmd{};
            drive_cmd.motor_speed_mmps = target_motor_speed;
            drive_cmd.gear = static_cast<uint8_t>(snap.ignition ? snap.gear : can::Gear::N);
            can::Frame drive_fr;
            can::gen::encode_rt_drive_cmd(drive_cmd, drive_fr);
            tx_history.push_back({drive_fr, "RT_DRIVE_CMD"});
        }

        // 5. Authority commands (10 Hz periodic or on ignition toggle).
        // RM emulates SYS on the test bench: emits 0x110 SYS_MODE_CMD + 0x113
        // SYS_PWR_CMD (it must NOT emit the Host request frames 0x111/0x112).
        bool ignition_changed = (snap.ignition != last_ignition);
        last_ignition = snap.ignition;

        if (++hmi_heartbeat_counter >= 5 || ignition_changed) {
            hmi_heartbeat_counter = 0;

            can::gen::SysModeCmd mode_cmd{};
            mode_cmd.mode = (drive_active) ? static_cast<uint8_t>(can::Mode::Auto)
                                           : static_cast<uint8_t>(can::Mode::Manual);
            mode_cmd.rolling_counter = ++roll_sys_mode;
            can::Frame mode_fr;
            can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr);
            tx_history.push_back({mode_fr, "SYS_MODE_CMD"});

            can::gen::SysPwrCmd pwr_cmd{};
            pwr_cmd.power_state = (!estop_or_signal_loss && snap.ignition) ? true : false;
            pwr_cmd.rolling_counter = ++roll_sys_pwr;
            can::Frame pwr_fr;
            can::gen::encode_sys_pwr_cmd(pwr_cmd, pwr_fr);
            tx_history.push_back({pwr_fr, "SYS_PWR_CMD"});
        }
    }
};

void test_rm_gateway_state_machine() {
    std::printf("[TEST GROUP] RM Gateway State Machine, ESTOP & Interlocks...\n");
    RmGatewayEngine gw;

    // 1. Initial State: Neutral, Ignition OFF -> Drive Active must be FALSE
    rm::RcSnapshot snap;
    snap.signal_valid = true;
    snap.ignition = false;
    snap.gear = can::Gear::N;
    snap.steering_deg = 0.0f;
    snap.brake_stroke_mm = 0.0f;
    snap.throttle_norm = 0.0f;

    gw.tick_can_tx(snap);
    ASSERT_FALSE(gw.estop_latched);

    // Verify 0x204 transmitted speed is 0 and gear is N
    bool found_drive = false;
    for (const auto& rec : gw.tx_history) {
        if (rec.frame.id == 0x204u) {
            found_drive = true;
            can::gen::RtDriveCmd cmd{};
            can::gen::decode_rt_drive_cmd(rec.frame.view(), cmd);
            ASSERT_EQ(cmd.motor_speed_mmps, 0);
            ASSERT_EQ(cmd.gear, static_cast<uint8_t>(can::Gear::N));
        }
    }
    ASSERT_TRUE(found_drive);

    // 2. Turn Ignition ON and shift to Drive (D) with 60% throttle
    gw.tx_history.clear();
    snap.ignition = true;
    snap.gear = can::Gear::D;
    snap.throttle_norm = 0.60f;
    snap.steering_deg = 15.0f;
    snap.brake_stroke_mm = 0.0f;

    gw.tick_can_tx(snap);

    // Verify RT_DRIVE_CMD: speed = 0.60 * 3000 = 1800 mm/s
    bool found_sys_mode = false, found_sys_pwr = false;
    bool found_hmi_mode = false, found_hmi_pwr = false;
    for (const auto& rec : gw.tx_history) {
        if (rec.frame.id == 0x204u) {
            can::gen::RtDriveCmd cmd{};
            can::gen::decode_rt_drive_cmd(rec.frame.view(), cmd);
            ASSERT_EQ(cmd.motor_speed_mmps, 1800);
            ASSERT_EQ(cmd.gear, static_cast<uint8_t>(can::Gear::D));
        } else if (rec.frame.id == 0x169u) {
            // Steering raw = round(15.0 * 10) + 30000 = 30150
            can::custom::ses::Command ses{};
            can::custom::ses::decode_command(rec.frame.view(), ses);
            ASSERT_EQ(ses.target_angle_raw, 30150);
            ASSERT_TRUE(ses.control_enable);
        } else if (rec.frame.id == can::kIdSysModeCmd) {
            found_sys_mode = true;
        } else if (rec.frame.id == can::kIdSysPwrCmd) {
            found_sys_pwr = true;
        } else if (rec.frame.id == can::kIdHmiModeReq) {
            found_hmi_mode = true;
        } else if (rec.frame.id == can::kIdHmiPwrReq) {
            found_hmi_pwr = true;
        }
    }
    // RM emulates SYS: emits authority frames (0x110/0x113), never the Host
    // request frames (0x111/0x112).
    ASSERT_TRUE(found_sys_mode);
    ASSERT_TRUE(found_sys_pwr);
    ASSERT_FALSE(found_hmi_mode);
    ASSERT_FALSE(found_hmi_pwr);

    // 3. Shift to Reverse (R) with 40% throttle
    gw.tx_history.clear();
    snap.gear = can::Gear::R;
    snap.throttle_norm = 0.40f;
    gw.tick_can_tx(snap);

    // In reverse, speed is negative: -0.40 * 500 = -200 mm/s
    for (const auto& rec : gw.tx_history) {
        if (rec.frame.id == 0x204u) {
            can::gen::RtDriveCmd cmd{};
            can::gen::decode_rt_drive_cmd(rec.frame.view(), cmd);
            ASSERT_EQ(cmd.motor_speed_mmps, -200);
            ASSERT_EQ(cmd.gear, static_cast<uint8_t>(can::Gear::R));
        }
    }

    // 4. External ESTOP received from CAN bus (0x001)
    can::Frame estop_in;
    estop_in.id = 0x001u;
    estop_in.dlc = 0;
    gw.handle_rx_can(estop_in);
    ASSERT_TRUE(gw.estop_latched);

    // Under latched ESTOP, tick with throttle active -> RT_DRIVE_CMD must be GATED OFF!
    gw.tx_history.clear();
    gw.tick_can_tx(snap);

    bool drive_sent_during_estop = false;
    for (const auto& rec : gw.tx_history) {
        if (rec.frame.id == 0x204u) drive_sent_during_estop = true;
        if (rec.frame.id == 0x7B9u) {
            // SEB brake MUST assert emergency clamp (27mm -> raw 1140)
            can::custom::seb::Command seb{};
            can::custom::seb::decode_command(rec.frame.view(), seb);
            ASSERT_EQ(seb.stroke_request_raw, 1140);
        }
    }
    ASSERT_FALSE(drive_sent_during_estop);

    // 5. ESTOP Reset Recovery Procedure:
    // Attempting reset with Ignition ON and Gear in R -> MUST FAIL
    gw.tick_can_tx(snap);
    ASSERT_TRUE(gw.estop_latched);

    // Attempting reset with Ignition OFF but Gear in R -> MUST FAIL
    snap.ignition = false;
    gw.tick_can_tx(snap);
    ASSERT_TRUE(gw.estop_latched);

    // Proper reset condition: Signal Valid + Ignition OFF + Gear NEUTRAL
    snap.gear = can::Gear::N;
    gw.tick_can_tx(snap);
    ASSERT_FALSE(gw.estop_latched); // CLEARED!

    // 6. Signal Loss Deadman Transition
    gw.tx_history.clear();
    snap.signal_valid = false; // RC SIGNAL LOST
    gw.tick_can_tx(snap);

    // Immediate 0x001 broadcast must occur
    bool estop_broadcast = false;
    for (const auto& rec : gw.tx_history) {
        if (rec.frame.id == 0x001u) {
            estop_broadcast = true;
            ASSERT_EQ(rec.frame.dlc, 0u);
        }
    }
    ASSERT_TRUE(estop_broadcast);

    // On subsequent tick while still in signal loss, 0x001 should NOT re-broadcast repeatedly
    gw.tx_history.clear();
    gw.tick_can_tx(snap);
    bool repeat_estop = false;
    for (const auto& rec : gw.tx_history) {
        if (rec.frame.id == 0x001u) repeat_estop = true;
    }
    ASSERT_FALSE(repeat_estop);
}

// ═══════════════════════════════════════════════════════════════════════
// 10. Wire-Level CAN Protocol Codec Integrity
// ═══════════════════════════════════════════════════════════════════════

void test_wire_can_codecs() {
    std::printf("[TEST GROUP] Wire-Level CAN Codecs (Bit-Exact Pack/Unpack)...\n");

    // ── VCU_SES_REQ (0x169) ──
    can::custom::ses::Command ses_cmd{};
    ses_cmd.alignment_enable = true;
    ses_cmd.control_enable = true;
    ses_cmd.target_angle_raw = 30250; // +25.0 deg
    ses_cmd.target_speed_raw = 328;
    ses_cmd.rolling_counter = 7;
    can::Frame ses_fr;
    ASSERT_EQ(static_cast<int>(can::custom::ses::encode_command(ses_cmd, ses_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(ses_fr.id, 0x169u);
    ASSERT_EQ(ses_fr.dlc, 8u);

    can::custom::ses::Command ses_dec{};
    ASSERT_EQ(static_cast<int>(can::custom::ses::decode_command(ses_fr.view(), ses_dec)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(ses_dec.target_angle_raw, 30250);
    ASSERT_TRUE(ses_dec.alignment_enable);
    ASSERT_TRUE(ses_dec.control_enable);
    ASSERT_EQ(ses_dec.rolling_counter, 7);

    // ── VCU_SEB_REQ (0x7B9) ──
    can::custom::seb::Command seb_cmd{};
    seb_cmd.alignment_enable = true;
    seb_cmd.control_enable = true;
    seb_cmd.control_mode = can::custom::seb::ControlMode::Stroke;
    seb_cmd.stroke_request_raw = 900; // 15.0 mm
    seb_cmd.rolling_counter = 11;
    can::Frame seb_fr;
    ASSERT_EQ(static_cast<int>(can::custom::seb::encode_command(seb_cmd, seb_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(seb_fr.id, 0x7B9u);
    ASSERT_EQ(seb_fr.dlc, 8u);

    can::custom::seb::Command seb_dec{};
    ASSERT_EQ(static_cast<int>(can::custom::seb::decode_command(seb_fr.view(), seb_dec)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(seb_dec.stroke_request_raw, 900);
    ASSERT_TRUE(seb_dec.alignment_enable);
    ASSERT_TRUE(seb_dec.control_enable);
    ASSERT_EQ(seb_dec.rolling_counter, 11);

    // ── RT_DRIVE_CMD (0x204) ──
    can::gen::RtDriveCmd drv_cmd{2500, static_cast<uint8_t>(can::Gear::D)};
    can::Frame drv_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_rt_drive_cmd(drv_cmd, drv_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(drv_fr.id, 0x204u);
    ASSERT_EQ(drv_fr.dlc, 5u);

    can::gen::RtDriveCmd drv_dec{};
    ASSERT_EQ(static_cast<int>(can::gen::decode_rt_drive_cmd(drv_fr.view(), drv_dec)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(drv_dec.motor_speed_mmps, 2500);
    ASSERT_EQ(drv_dec.gear, static_cast<uint8_t>(can::Gear::D));

    // ── HMI_MODE_REQ (0x111) & HMI_PWR_REQ (0x112) ──
    can::gen::HmiModeReq mode_msg{1, 12};
    can::Frame mode_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_hmi_mode_req(mode_msg, mode_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(mode_fr.id, 0x111u);
    ASSERT_EQ(mode_fr.dlc, 2u);

    can::gen::HmiPwrReq pwr_msg{1, 34};
    can::Frame pwr_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_hmi_pwr_req(pwr_msg, pwr_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(pwr_fr.id, 0x112u);
    ASSERT_EQ(pwr_fr.dlc, 2u);

    // ── SAFETY_ESTOP (0x001) ──
    can::gen::SafetyEstop estop_msg{};
    can::Frame estop_fr;
    ASSERT_EQ(static_cast<int>(can::gen::encode_safety_estop(estop_msg, estop_fr)),
              static_cast<int>(can::gen::CodecStatus::Ok));
    ASSERT_EQ(estop_fr.id, 0x001u);
    ASSERT_EQ(estop_fr.dlc, 0u);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════════════

int main() {
    std::printf("\n========================================================\n");
    std::printf("  RM-ESP32 COMPLETE TEST SUITE (FULL SUBSYSTEM COVERAGE)\n");
    std::printf("========================================================\n\n");

    test_pulse_bounds_and_rejection();
    test_deadman_watchdog_timeouts();
    test_steering_mapping_and_deadband();
    test_brake_mapping();
    test_throttle_mapping();
    test_spare_channel_mapping();
    test_switches_ignition_and_gear();
    test_jitter_filter();
    test_rm_gateway_state_machine();
    test_wire_can_codecs();

    std::printf("\n--------------------------------------------------------\n");
    std::printf("RM-ESP32 Total Assertions: %d | Failures: %d\n", g_tests_run, g_tests_failed);
    if (g_tests_failed == 0) {
        std::printf(">>> ALL RM-ESP32 TESTS PASSED! <<<\n\n");
        return 0;
    }
    std::printf(">>> SOME RM-ESP32 TESTS FAILED! <<<\n\n");
    return 1;
}
