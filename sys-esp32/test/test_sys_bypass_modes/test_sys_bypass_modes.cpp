#include <unity.h>
#include <cstdint>
#include "config.h"
#include "bypass_modes.h"
#include "relay_pipeline.h"

// The runtime bypass and ControlPath state are defined by the firmware main.cpp
// (excluded from the native test build). The bypass tests exercise the SHARED pure
// decision function (bypass_modes.h) and relay pipeline (relay_pipeline.h).
etrike::ControlPath g_control_path = etrike::ControlPath::Production;
etrike::HighLevelLossPolicy g_host_loss_policy = etrike::HighLevelLossPolicy::ZeroOutput;
uint32_t g_host_loss_timeout_ms = 500;
bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = false;
bool g_bypass_seb_sync = false;
bool g_bypass_mtr_absent = false;

static void evaluate_bypass_modes(int run_mode, bool override_pin_low) {
    const etrike::BypassState b = etrike::evaluate_run_mode_bypasses(run_mode, override_pin_low);
    g_control_path = b.control_path;
    g_bench_solo_mode = b.bench_solo_mode;
    g_bypass_eps_sync = b.bypass_eps_sync;
    g_bypass_seb_sync = b.bypass_seb_sync;
    g_bypass_mtr_absent = b.bypass_mtr_absent;
}

void setUp(void) {
    g_control_path = etrike::ControlPath::Production;
    g_bench_solo_mode = false;
    g_bypass_eps_sync = false;
    g_bypass_seb_sync = false;
    g_bypass_mtr_absent = false;
}

void tearDown(void) {}

void test_sys_bypass_modes(void) {
    // 1. Vehicle Production Mode (run_mode = 0) -> All bypasses FALSE, ControlPath::Production
    evaluate_bypass_modes(0, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::Production),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 2. Hardware Prototype without Jumper (run_mode = 1, pin HIGH) -> Safety Enforced
    evaluate_bypass_modes(1, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::Production),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_FALSE(g_bench_solo_mode);
    TEST_ASSERT_FALSE(g_bypass_eps_sync);
    TEST_ASSERT_FALSE(g_bypass_seb_sync);
    TEST_ASSERT_FALSE(g_bypass_mtr_absent);

    // 3. Hardware Prototype WITH Jumper (run_mode = 1, pin LOW) -> BenchPassthrough Active
    evaluate_bypass_modes(1, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::BenchPassthrough),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_TRUE(g_bench_solo_mode);
    TEST_ASSERT_TRUE(g_bypass_eps_sync);
    TEST_ASSERT_TRUE(g_bypass_seb_sync);
    TEST_ASSERT_TRUE(g_bypass_mtr_absent);

    // 4. Bench Simulation Mode (run_mode = 2) -> BenchPassthrough Active
    evaluate_bypass_modes(2, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::BenchPassthrough),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_TRUE(g_bench_solo_mode);

    evaluate_bypass_modes(2, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::ControlPath::BenchPassthrough),
                            static_cast<uint8_t>(g_control_path));
    TEST_ASSERT_TRUE(g_bench_solo_mode);
}

// Rule 8b & Rule 11 Test: High-level HMI mode/power -> SYS BenchPassthrough -> same command
// regardless of START button state, mode state, or active inhibits.
void test_sys_bench_passthrough_mode_and_power_relay_ignores_start_and_inhibits(void) {
    bool mode_auto = false;
    bool power_on = false;

    // BenchPassthrough: Even with START unlatched (run_enabled=false) and inhibit set,
    // HMI request for AUTO and POWER_ON is directly relayed!
    sys::compute_mode_and_pwr_relay(
        etrike::ControlPath::BenchPassthrough,
        can::Mode::Auto,
        /*power_requested=*/true,
        /*run_enabled=*/false, // START button released!
        /*any_inhibit=*/true,   // Inhibits active!
        mode_auto,
        power_on
    );

    TEST_ASSERT_TRUE(mode_auto);
    TEST_ASSERT_TRUE(power_on);

    // Verify contrast in Production: safety arbitration strictly blocks authority
    sys::compute_mode_and_pwr_relay(
        etrike::ControlPath::Production,
        can::Mode::Auto,
        /*power_requested=*/true,
        /*run_enabled=*/false,
        /*any_inhibit=*/false,
        mode_auto,
        power_on
    );

    TEST_ASSERT_FALSE(mode_auto);
    TEST_ASSERT_FALSE(power_on);
}

// Rule 8b & Rule 11 Test: RT brake command -> SYS BenchPassthrough -> SEB command
// without waiting for SEB stroke alignment or status feedback.
void test_sys_bench_passthrough_brake_relay_ungated(void) {
    can::custom::seb::Command seb_cmd{};
    sys::compute_brake_relay(
        etrike::ControlPath::BenchPassthrough,
        2800 /* 2800 kPa brake request */,
        seb_cmd
    );

    TEST_ASSERT_EQUAL_UINT8(56, seb_cmd.pressure_request_raw);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::protocol::codecs::seb::ControlMode::Pressure),
                            static_cast<uint8_t>(seb_cmd.control_mode));
}

// Realistic Condition 1: Immunity to random driver switch toggling / button interference
// In physical bench tests, operators often press buttons or pull brake levers.
// BenchPassthrough must NEVER drop authority or cut power when physical switches flutter.
void test_sys_bench_passthrough_driver_interference_immunity(void) {
    bool mode_auto = false;
    bool power_on = false;

    // Simulate 20 continuous cycles with fluctuating physical inputs (START released, inhibits fluttering)
    for (int cycle = 0; cycle < 20; ++cycle) {
        const bool run_enabled = (cycle % 2 == 0); // START switch randomly toggling
        const bool any_inhibit = (cycle % 3 == 0); // Inhibits randomly appearing

        sys::compute_mode_and_pwr_relay(
            etrike::ControlPath::BenchPassthrough,
            can::Mode::Auto,
            /*power_requested=*/true,
            run_enabled,
            any_inhibit,
            mode_auto,
            power_on
        );

        // Invariant: BenchPassthrough MUST remain locked in Auto and Power ON regardless of physical switch state!
        TEST_ASSERT_TRUE(mode_auto);
        TEST_ASSERT_TRUE(power_on);
    }
}

// Realistic Condition 2: Continuous dynamic brake pressure sweep from 0 to 5000 kPa
// Verifies raw scaling (kpa + 25) / 50 and wire format encoding across the entire physical range
void test_sys_bench_passthrough_dynamic_brake_sweeps(void) {
    can::custom::seb::Command seb_cmd{};

    // 0 kPa -> Stroke mode, 0 pressure raw
    sys::compute_brake_relay(etrike::ControlPath::BenchPassthrough, 0, seb_cmd);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::protocol::codecs::seb::ControlMode::Stroke),
                            static_cast<uint8_t>(seb_cmd.control_mode));
    TEST_ASSERT_EQUAL_UINT8(0, seb_cmd.pressure_request_raw);

    // Continuous dynamic pressure ramp 100 kPa -> 5000 kPa in steps of 250 kPa
    for (int32_t kpa = 250; kpa <= 5000; kpa += 250) {
        sys::compute_brake_relay(etrike::ControlPath::BenchPassthrough, kpa, seb_cmd);

        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(etrike::protocol::codecs::seb::ControlMode::Pressure),
                                static_cast<uint8_t>(seb_cmd.control_mode));
        const uint8_t expected_raw = static_cast<uint8_t>(std::min<int32_t>(100, (kpa + 25) / 50));
        TEST_ASSERT_EQUAL_UINT8(expected_raw, seb_cmd.pressure_request_raw);

        // Encode to wire format CAN frame (0x7B9) and decode
        can::Frame fr{};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                                static_cast<uint8_t>(can::custom::seb::encode_command(seb_cmd, fr)));
        can::custom::seb::Command dec{};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                                static_cast<uint8_t>(can::custom::seb::decode_command(fr, dec)));
        TEST_ASSERT_EQUAL_UINT8(expected_raw, dec.pressure_request_raw);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(seb_cmd.control_mode),
                                static_cast<uint8_t>(dec.control_mode));
    }
}

// Realistic Condition 3: Full wire format HMI request -> SYS relay -> command frame roundtrip
void test_sys_bench_passthrough_wire_format_mode_and_pwr_relay(void) {
    // 1. Simulate HMI sending Mode (0x111) and Power (0x112) on CAN wire
    can::gen::HmiModeReq hmi_mode_wire{};
    hmi_mode_wire.req_mode = true; // Auto
    hmi_mode_wire.rolling_counter = 7;
    can::Frame f_111{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::encode_hmi_mode_req(hmi_mode_wire, f_111)));

    can::gen::HmiPwrReq hmi_pwr_wire{};
    hmi_pwr_wire.req_start = true; // Power On
    hmi_pwr_wire.rolling_counter = 7;
    can::Frame f_112{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::encode_hmi_pwr_req(hmi_pwr_wire, f_112)));

    // Decode from wire
    can::gen::HmiModeReq dec_hmi_m{};
    can::gen::HmiPwrReq dec_hmi_p{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::decode_hmi_mode_req(f_111.view(), dec_hmi_m)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::decode_hmi_pwr_req(f_112.view(), dec_hmi_p)));

    // Relay through pure SYS pipeline
    bool mode_auto = false;
    bool power_on = false;
    sys::compute_mode_and_pwr_relay(
        etrike::ControlPath::BenchPassthrough,
        dec_hmi_m.req_mode ? can::Mode::Auto : can::Mode::Manual,
        dec_hmi_p.req_start,
        /*run_enabled=*/false, // START button released!
        /*any_inhibit=*/true,   // Inhibits active!
        mode_auto,
        power_on
    );

    // 2. Encode to outgoing wire frames: 0x110 (SYS_MODE_CMD) and 0x113 (SYS_PWR_CMD)
    can::gen::SysModeCmd sys_mode_out{};
    sys_mode_out.mode = mode_auto ? 1u : 0u;
    sys_mode_out.rolling_counter = 12;
    can::Frame f_110{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::encode_sys_mode_cmd(sys_mode_out, f_110)));

    can::gen::SysPwrCmd sys_pwr_out{};
    sys_pwr_out.power_state = power_on ? 1u : 0u;
    sys_pwr_out.rolling_counter = 12;
    can::Frame f_113{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::encode_sys_pwr_cmd(sys_pwr_out, f_113)));

    // 3. Decode outgoing wire frames and verify wire bits
    can::gen::SysModeCmd dec_110{};
    can::gen::SysPwrCmd dec_113{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::decode_sys_mode_cmd(f_110.view(), dec_110)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(can::gen::CodecStatus::Ok),
                            static_cast<uint8_t>(can::gen::decode_sys_pwr_cmd(f_113.view(), dec_113)));

    TEST_ASSERT_EQUAL_UINT8(1, dec_110.mode);
    TEST_ASSERT_EQUAL_UINT8(12, dec_110.rolling_counter);
    TEST_ASSERT_EQUAL_UINT8(1, dec_113.power_state);
    TEST_ASSERT_EQUAL_UINT8(12, dec_113.rolling_counter);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_bypass_modes);
    RUN_TEST(test_sys_bench_passthrough_mode_and_power_relay_ignores_start_and_inhibits);
    RUN_TEST(test_sys_bench_passthrough_brake_relay_ungated);
    RUN_TEST(test_sys_bench_passthrough_driver_interference_immunity);
    RUN_TEST(test_sys_bench_passthrough_dynamic_brake_sweeps);
    RUN_TEST(test_sys_bench_passthrough_wire_format_mode_and_pwr_relay);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
