#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <deque>
#include <vector>

// 1. Include STM32 HAL stub before subsystem headers
#include "stub/stm32g4xx_hal.h"

// Define dummy global FDCAN handle needed by CanDriver extern "C"
extern "C" {
    FDCAN_HandleTypeDef hfdcan1;
}

// 2. Include actual production MTR headers
#include "mtr-stm32/src/config.h"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"
#include "mtr-stm32/src/can_driver.h"
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

// ═══════════════════════════════════════════════════════════════════════
// 1. Relay Controller: Mutual Exclusion & Active-Low Polarity
// ═══════════════════════════════════════════════════════════════════════

void test_relay_controller_mutual_exclusion() {
    std::printf("[TEST GROUP] Relay Controller: Mutual Exclusion & Polarity...\n");
    hal_mock::reset();

    mtr::RelayController relays;

    // 1. Initial State before and after init()
    relays.init();
    // After init, all relay pins (kRelayRevPin, kRelayDrivePin, kRelayIgnitionPin) must be SET (OFF)
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    ASSERT_EQ(relays.current_gear(), can::Gear::N);
    ASSERT_FALSE(relays.is_ignition_on());

    // Active-low check: GPIO_PIN_SET means relay is DE-ENERGIZED (OFF)
    // GPIOA pin 0 (Rev), pin 2 (Drive), pin 4 (Ignition) should all have bits set
    uint16_t all_relay_mask = mtr::kRelayRevPin | mtr::kRelayDrivePin | mtr::kRelayIgnitionPin;
    ASSERT_EQ(hal_mock::g_gpio_a_pins & all_relay_mask, all_relay_mask);

    // 2. Set to Drive (Gear::D, ignition_on = true)
    relays.set_gear(can::Gear::D, true);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_EQ(relays.current_gear(), can::Gear::D);
    ASSERT_TRUE(relays.is_ignition_on());

    // Check individual pin levels:
    // Drive pin (PA2) = RESET (0), Ignition pin (PA4) = RESET (0), Reverse pin (PA0) = SET (1)
    bool drive_pin_on = (hal_mock::g_gpio_a_pins & mtr::kRelayDrivePin) == 0;
    bool rev_pin_on   = (hal_mock::g_gpio_a_pins & mtr::kRelayRevPin) == 0;
    bool ign_pin_on   = (hal_mock::g_gpio_a_pins & mtr::kRelayIgnitionPin) == 0;

    ASSERT_TRUE(drive_pin_on);
    ASSERT_FALSE(rev_pin_on);
    ASSERT_TRUE(ign_pin_on);

    // HARD CRITICAL SAFETY CHECK: Drive and Reverse must NEVER be ON at the same time
    ASSERT_FALSE(drive_pin_on && rev_pin_on);

    // 3. Set to Reverse (Gear::R, ignition_on = true)
    relays.set_gear(can::Gear::R, true);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Reverse);
    ASSERT_EQ(relays.current_gear(), can::Gear::R);
    ASSERT_TRUE(relays.is_ignition_on());

    drive_pin_on = (hal_mock::g_gpio_a_pins & mtr::kRelayDrivePin) == 0;
    rev_pin_on   = (hal_mock::g_gpio_a_pins & mtr::kRelayRevPin) == 0;
    ign_pin_on   = (hal_mock::g_gpio_a_pins & mtr::kRelayIgnitionPin) == 0;

    ASSERT_FALSE(drive_pin_on);
    ASSERT_TRUE(rev_pin_on);
    ASSERT_TRUE(ign_pin_on);
    ASSERT_FALSE(drive_pin_on && rev_pin_on); // Mutual exclusion verified!

    // 4. Set to Park / Neutral (Gear::N, ignition_on = true)
    relays.set_gear(can::Gear::N, true);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Park);
    ASSERT_EQ(relays.current_gear(), can::Gear::N);
    ASSERT_TRUE(relays.is_ignition_on());

    drive_pin_on = (hal_mock::g_gpio_a_pins & mtr::kRelayDrivePin) == 0;
    rev_pin_on   = (hal_mock::g_gpio_a_pins & mtr::kRelayRevPin) == 0;
    ign_pin_on   = (hal_mock::g_gpio_a_pins & mtr::kRelayIgnitionPin) == 0;

    ASSERT_FALSE(drive_pin_on);
    ASSERT_FALSE(rev_pin_on);
    ASSERT_TRUE(ign_pin_on);

    // 5. Gear D with Ignition OFF -> MUST force State::Off
    relays.set_gear(can::Gear::D, false);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    ASSERT_FALSE(relays.is_ignition_on());
    ASSERT_EQ(hal_mock::g_gpio_a_pins & all_relay_mask, all_relay_mask);

    // 6. Status LED Toggles on state changes
    uint32_t toggles_before = hal_mock::g_led_toggle_count;
    relays.set_state(mtr::RelayController::State::Drive);
    ASSERT_EQ(hal_mock::g_led_toggle_count, toggles_before + 1);

    // Re-asserting same state must NOT toggle LED
    relays.set_state(mtr::RelayController::State::Drive);
    ASSERT_EQ(hal_mock::g_led_toggle_count, toggles_before + 1);
}

// ═══════════════════════════════════════════════════════════════════════
// 2. Software I2C & MCP4725 DAC Controller
// ═══════════════════════════════════════════════════════════════════════

void test_dac_controller_software_i2c_and_clamps() {
    std::printf("[TEST GROUP] MCP4725 Software I2C & Voltage Clamping...\n");
    hal_mock::reset();

    mtr::DacController dac;
    dac.init();
    ASSERT_EQ(dac.current_code(), 0);

    // 1. Standard active write with default address 0x60 (0xC0)
    hal_mock::g_i2c_target_addr = 0x60 << 1; // 0xC0
    dac.set_throttle(1000, true);
    ASSERT_EQ(dac.current_code(), 1000);
    ASSERT_EQ(hal_mock::g_last_dac_written, 1000);

    // 2. Voltage Safety Window Clamping:
    // Minimum active code is 655 (~0.8V)
    dac.set_throttle(100, true);
    ASSERT_EQ(dac.current_code(), mtr::kDacMinCode); // Clamped to 655
    ASSERT_EQ(hal_mock::g_last_dac_written, mtr::kDacMinCode);

    dac.set_throttle(654, true);
    ASSERT_EQ(dac.current_code(), mtr::kDacMinCode);

    dac.set_throttle(655, true);
    ASSERT_EQ(dac.current_code(), 655);

    // Maximum active code is 1966 (~2.4V)
    dac.set_throttle(1966, true);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode);

    dac.set_throttle(2500, true);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode); // Clamped to 1966
    ASSERT_EQ(hal_mock::g_last_dac_written, mtr::kDacMaxCode);

    dac.set_throttle(5000, true);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode); // Out of 12-bit range clamped to 1966

    // 3. Zero / Disable Enforcement:
    // code = 0 -> outputs 0.0V
    dac.set_throttle(0, true);
    ASSERT_EQ(dac.current_code(), 0);
    ASSERT_EQ(hal_mock::g_last_dac_written, 0);

    // enabled = false -> outputs 0.0V regardless of requested code
    dac.set_throttle(1500, false);
    ASSERT_EQ(dac.current_code(), 0);
    ASSERT_EQ(hal_mock::g_last_dac_written, 0);

    // force_zero() -> outputs 0.0V
    dac.set_throttle(1200, true);
    ASSERT_EQ(dac.current_code(), 1200);
    dac.force_zero();
    ASSERT_EQ(dac.current_code(), 0);
    ASSERT_EQ(hal_mock::g_last_dac_written, 0);

    // 4. Multi-Address Probing (0x60, 0x61, 0x62)
    // Switch target address to candidate 0x61 (0xC2)
    hal_mock::g_i2c_target_addr = 0x61 << 1;
    // Current cache holds 0x60 -> first probe will NACK and invalidate cache, then successfully probe 0x61!
    dac.set_throttle(1400, true);
    ASSERT_EQ(dac.current_code(), 1400);
    ASSERT_EQ(hal_mock::g_last_dac_written, 1400);
}

// ═══════════════════════════════════════════════════════════════════════
// 3. Motor Manager: ESTOP & Recovery Transitions
// ═══════════════════════════════════════════════════════════════════════

void test_motor_manager_estop_and_recovery() {
    std::printf("[TEST GROUP] Motor Manager: ESTOP & Recovery...\n");
    hal_mock::reset();

    mtr::RelayController relays;
    mtr::DacController dac;
    mtr::MotorManager mgr(relays, dac);

    mgr.init();
    ASSERT_FALSE(mgr.is_estop_active());

    // 1. Establish normal driving state in Drive
    // Power ON (0x112)
    can::gen::HmiPwrReq pwr{1, 1};
    can::Frame pwr_fr;
    can::gen::encode_hmi_pwr_req(pwr, pwr_fr);
    mgr.handle_frame(pwr_fr, 100);

    // Drive command 2000 mm/s in Drive (0x204)
    can::gen::RtDriveCmd drv{2000, static_cast<uint8_t>(can::Gear::D)};
    can::Frame drv_fr;
    can::gen::encode_rt_drive_cmd(drv, drv_fr);
    mgr.handle_frame(drv_fr, 100);

    mgr.tick(100);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);

    // 2. Trigger ESTOP via 0x001 SAFETY_ESTOP
    can::Frame estop_fr;
    estop_fr.id = can::kIdSafetyEstop; // 0x001
    estop_fr.dlc = 0;
    mgr.handle_frame(estop_fr, 105);

    ASSERT_TRUE(mgr.is_estop_active());
    mgr.tick(105);

    // Relays forced to Off, DAC forced to 0V
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    ASSERT_EQ(dac.current_code(), 0);

    // Gap #15 Redundant ESTOP ACK check in 0x206 MTR_MOTOR_FBK
    can::Frame fbk_fr = mgr.build_motor_feedback_frame();
    can::gen::MtrMotorFbk fbk{};
    can::gen::decode_mtr_motor_fbk(fbk_fr.view(), fbk);
    ASSERT_TRUE((fbk.fault_flags & shared::kMtrFaultEstopActive) != 0);
    ASSERT_TRUE((fbk.fault_flags & shared::kMtrFaultStartupReady) != 0);

    // 3. Mode recovery sequence via 0x110 SYS_MODE_CMD
    // Remaining in Estop mode (mode = 2) -> ESTOP stays latched
    can::gen::SysModeCmd mode_cmd{static_cast<uint8_t>(can::Mode::Estop)};
    can::Frame mode_fr;
    can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr);
    mgr.handle_frame(mode_fr, 110);
    ASSERT_TRUE(mgr.is_estop_active());

    // Transition to Manual mode (mode = 0) -> CLEARS ESTOP!
    mode_cmd.mode = static_cast<uint8_t>(can::Mode::Manual);
    can::gen::encode_sys_mode_cmd(mode_cmd, mode_fr);
    mgr.handle_frame(mode_fr, 120);
    ASSERT_FALSE(mgr.is_estop_active());

    // Now tick again with valid drive frame -> Motor resumes!
    mgr.handle_frame(drv_fr, 120);
    mgr.tick(120);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);
}

// ═══════════════════════════════════════════════════════════════════════
// 4. Motor Manager: Direction Shift Arc-Protection Dwell (50 ms)
// ═══════════════════════════════════════════════════════════════════════

void test_motor_manager_direction_shift_dwell() {
    std::printf("[TEST GROUP] Direction Shift Arc-Protection Dwell (50 ms)...\n");
    hal_mock::reset();

    mtr::RelayController relays;
    mtr::DacController dac;
    mtr::MotorManager mgr(relays, dac);
    mgr.init();

    // Ignition ON
    can::gen::HmiPwrReq pwr{1, 1};
    can::Frame pwr_fr;
    can::gen::encode_hmi_pwr_req(pwr, pwr_fr);
    mgr.handle_frame(pwr_fr, 100);

    // 1. Cruising in Drive (D) at 1500 mm/s
    can::gen::RtDriveCmd drv_cmd{1500, static_cast<uint8_t>(can::Gear::D)};
    can::Frame drv_fr;
    can::gen::encode_rt_drive_cmd(drv_cmd, drv_fr);
    mgr.handle_frame(drv_fr, 100);
    mgr.tick(100);

    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);

    // 2. Immediate reversal requested: shift to Reverse (R) at t = 200 ms
    can::gen::RtDriveCmd rev_cmd{300, static_cast<uint8_t>(can::Gear::R)};
    can::Frame rev_fr;
    can::gen::encode_rt_drive_cmd(rev_cmd, rev_fr);
    mgr.handle_frame(rev_fr, 200);

    // First tick at t = 200 ms (direction shift detected):
    mgr.tick(200);

    // Arc-protection dwell MUST immediately drop DAC to 0 and shift relays to Neutral (Park)
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Park);
    ASSERT_EQ(dac.current_code(), 0);

    // 3. Check during dwell: at t = 230 ms (30 ms elapsed < 50 ms dwell)
    mgr.tick(230);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Park); // Still in neutral!
    ASSERT_EQ(dac.current_code(), 0);

    // 4. Check at expiration of dwell: at t = 250 ms (50 ms elapsed = kShiftDwellMs)
    mgr.tick(250);
    // Dwell complete! Reverse relay now energizes and DAC activates!
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Reverse);
    ASSERT_TRUE(dac.current_code() > 0);

    // 5. Shift back from Reverse (R) to Drive (D) at t = 300 ms
    mgr.handle_frame(drv_fr, 300);
    mgr.tick(300);

    // Must immediately enter neutral dwell with DAC 0
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Park);
    ASSERT_EQ(dac.current_code(), 0);

    // At t = 325 ms (mid-dwell)
    mgr.tick(325);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Park);
    ASSERT_EQ(dac.current_code(), 0);

    // At t = 350 ms (dwell finished)
    mgr.tick(350);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);
}

// ═══════════════════════════════════════════════════════════════════════
// 5. Motor Manager: Comms Watchdog Timeout (500 ms)
// ═══════════════════════════════════════════════════════════════════════

void test_motor_manager_watchdog_timeout() {
    std::printf("[TEST GROUP] Comms Watchdog Timeout (500 ms)...\n");
    hal_mock::reset();

    mtr::RelayController relays;
    mtr::DacController dac;
    mtr::MotorManager mgr(relays, dac);
    mgr.init();

    // Ignition ON
    can::gen::HmiPwrReq pwr{1, 1};
    can::Frame pwr_fr;
    can::gen::encode_hmi_pwr_req(pwr, pwr_fr);
    mgr.handle_frame(pwr_fr, 1000);

    // Drive command at t = 1000 ms
    can::gen::RtDriveCmd drv{2000, static_cast<uint8_t>(can::Gear::D)};
    can::Frame drv_fr;
    can::gen::encode_rt_drive_cmd(drv, drv_fr);
    mgr.handle_frame(drv_fr, 1000);
    mgr.tick(1000);

    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);

    // Elapsed 500 ms (t = 1500 ms) without new frames -> exactly at threshold, not timed out
    mgr.tick(1500);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);

    // Elapsed 501 ms (t = 1501 ms) -> TIMEOUT!
    mgr.tick(1501);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    ASSERT_EQ(dac.current_code(), 0);

    // At t = 1800 ms (still silent)
    mgr.tick(1800);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    ASSERT_EQ(dac.current_code(), 0);

    // Frame resumes at t = 2000 ms -> Watchdog recovers automatically
    mgr.handle_frame(drv_fr, 2000);
    mgr.tick(2000);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);
}

// ═══════════════════════════════════════════════════════════════════════
// 6. Motor Manager: Speed-to-DAC Curve & Reverse Magnitude
// ═══════════════════════════════════════════════════════════════════════

void test_motor_manager_dac_curves() {
    std::printf("[TEST GROUP] Speed-to-DAC Transfer Curves & Reverse Scaling...\n");
    hal_mock::reset();

    mtr::RelayController relays;
    mtr::DacController dac;
    mtr::MotorManager mgr(relays, dac);
    mgr.init();

    can::gen::HmiPwrReq pwr{1, 1};
    can::Frame pwr_fr;
    can::gen::encode_hmi_pwr_req(pwr, pwr_fr);
    mgr.handle_frame(pwr_fr, 100);

    // Forward curve: 0 .. 3000 mm/s -> 655 .. 1966
    // Zero speed in Drive -> DAC 0
    can::gen::RtDriveCmd drv{0, static_cast<uint8_t>(can::Gear::D)};
    can::Frame fr;
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 100);
    mgr.tick(100);
    ASSERT_EQ(dac.current_code(), 0);

    // 1500 mm/s (50% forward) -> midpoint = 700 + 0.5 * (1966 - 700) = 1333
    drv.motor_speed_mmps = 1500;
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 100);
    mgr.tick(100);
    ASSERT_NEAR(dac.current_code(), 1333, 5);

    // 3000 mm/s (100% forward) -> max code 1966
    drv.motor_speed_mmps = 3000;
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 100);
    mgr.tick(100);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode);

    // Over-speed 4000 mm/s -> clamped to 1966
    drv.motor_speed_mmps = 4000;
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 100);
    mgr.tick(100);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode);

    // Reverse curve: max reverse speed = 500 mm/s
    // Shift to Reverse (dwell 50ms first)
    drv.gear = static_cast<uint8_t>(can::Gear::R);
    drv.motor_speed_mmps = -250; // 50% reverse speed (negative)
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 100);
    mgr.tick(100); // starts dwell
    mgr.tick(160); // dwell complete

    // Midpoint reverse (-250 mm/s) -> midpoint = 1333
    ASSERT_NEAR(dac.current_code(), 1333, 5);

    // Max reverse speed (-500 mm/s) -> 1966
    drv.motor_speed_mmps = -500;
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 160);
    mgr.tick(160);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode);

    // Positive magnitude in Reverse (+500 mm/s) -> 1966
    drv.motor_speed_mmps = 500;
    can::gen::encode_rt_drive_cmd(drv, fr);
    mgr.handle_frame(fr, 160);
    mgr.tick(160);
    ASSERT_EQ(dac.current_code(), mtr::kDacMaxCode);
}

// ═══════════════════════════════════════════════════════════════════════
// 7. Motor Manager: Legacy RM Fallback Frames (0x0BB, 0x0AA)
// ═══════════════════════════════════════════════════════════════════════

void test_motor_manager_legacy_fallback() {
    std::printf("[TEST GROUP] Legacy RM Fallback Frames (0x0BB & 0x0AA)...\n");
    hal_mock::reset();

    mtr::RelayController relays;
    mtr::DacController dac;
    mtr::MotorManager mgr(relays, dac);
    mgr.init();

    // 1. 0x0BB RM_RELAY_STATE = 0x05 (Drive + Ignition ON)
    can::Frame relay_fr;
    relay_fr.id = 0x0BB;
    relay_fr.dlc = 1;
    relay_fr.data[0] = 0x05;
    mgr.handle_frame(relay_fr, 100);

    // 0x0AA RM_THROTTLE_RAW = 32768 (50% throttle -> 1500 mm/s)
    can::Frame throt_fr;
    throt_fr.id = 0x0AA;
    throt_fr.dlc = 2;
    throt_fr.data[0] = 0x00; // Little endian 0x8000 = 32768
    throt_fr.data[1] = 0x80;
    mgr.handle_frame(throt_fr, 100);

    mgr.tick(100);

    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_EQ(mgr.target_speed_mmps(), 1500);
    ASSERT_NEAR(dac.current_code(), 1333, 10);

    // 2. 0x0BB = 0x09 (Reverse)
    relay_fr.data[0] = 0x09;
    mgr.handle_frame(relay_fr, 200);
    mgr.tick(200); // starts shift dwell
    mgr.tick(260); // dwell complete
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Reverse);

    // 3. 0x0BB = 0x03 (Park / Neutral)
    relay_fr.data[0] = 0x03;
    mgr.handle_frame(relay_fr, 300);
    mgr.tick(300);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Park);
    ASSERT_EQ(dac.current_code(), 0);

    // 4. 0x0BB = 0x00 (Off)
    relay_fr.data[0] = 0x00;
    mgr.handle_frame(relay_fr, 400);
    mgr.tick(400);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    ASSERT_FALSE(relays.is_ignition_on());
    ASSERT_EQ(dac.current_code(), 0);
}

// ═══════════════════════════════════════════════════════════════════════
// 8. FDCAN Driver: FIFO 0 Ringbuffer Mechanics
// ═══════════════════════════════════════════════════════════════════════

void test_fdcan_driver_ringbuffer() {
    std::printf("[TEST GROUP] FDCAN Driver: Ringbuffer & Filter Mechanics...\n");
    fdcan_mock::reset();

    mtr::CanDriver driver(hfdcan1);
    ASSERT_TRUE(driver.init());

    // Test FIFO push and poll
    can::Frame fr_out;
    ASSERT_FALSE(driver.poll_rx(fr_out)); // Empty ringbuffer

    // Inject 3 frames via ISR callback
    for (uint32_t id : {0x001u, 0x110u, 0x204u}) {
        fdcan_mock::MockMsg m{};
        m.id = id;
        m.ext = false;
        m.dlc = 8;
        m.data[0] = static_cast<uint8_t>(id & 0xFF);
        fdcan_mock::g_rx_queue.push_back(m);
    }

    driver.handle_rx_fifo0_isr();

    // Verify FIFO ordering: 0x001, 0x110, 0x204
    ASSERT_TRUE(driver.poll_rx(fr_out));
    ASSERT_EQ(fr_out.id, 0x001u);

    ASSERT_TRUE(driver.poll_rx(fr_out));
    ASSERT_EQ(fr_out.id, 0x110u);

    ASSERT_TRUE(driver.poll_rx(fr_out));
    ASSERT_EQ(fr_out.id, 0x204u);

    ASSERT_FALSE(driver.poll_rx(fr_out)); // Exhausted

    // Test Ringbuffer Overflow (size is 32)
    // Push 35 frames
    for (int i = 0; i < 35; ++i) {
        fdcan_mock::MockMsg m{};
        m.id = 0x204u;
        m.ext = false;
        m.dlc = 1;
        fdcan_mock::g_rx_queue.push_back(m);
    }
    driver.handle_rx_fifo0_isr();

    // Should detect overflow and count drops
    ASSERT_TRUE(driver.rx_overflow() > 0);

    // Test Transmit path
    can::Frame tx_frame(0x206, false, 4);
    tx_frame.data[0] = 0xAA;
    ASSERT_TRUE(driver.send(tx_frame));
    ASSERT_EQ(driver.tx_count(), 1u);
    ASSERT_EQ(fdcan_mock::g_tx_msgs.size(), 1u);
    ASSERT_EQ(fdcan_mock::g_tx_msgs[0].id, 0x206u);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════════════

int main() {
    std::printf("\n========================================================\n");
    std::printf("  MTR-STM32 COMPLETE TEST SUITE (FULL SUBSYSTEM COVERAGE)\n");
    std::printf("========================================================\n\n");

    test_relay_controller_mutual_exclusion();
    test_dac_controller_software_i2c_and_clamps();
    test_motor_manager_estop_and_recovery();
    test_motor_manager_direction_shift_dwell();
    test_motor_manager_watchdog_timeout();
    test_motor_manager_dac_curves();
    test_motor_manager_legacy_fallback();
    test_fdcan_driver_ringbuffer();

    std::printf("\n--------------------------------------------------------\n");
    std::printf("MTR-STM32 Total Assertions: %d | Failures: %d\n", g_tests_run, g_tests_failed);
    if (g_tests_failed == 0) {
        std::printf(">>> ALL MTR-STM32 TESTS PASSED! <<<\n\n");
        return 0;
    }
    std::printf(">>> SOME MTR-STM32 TESTS FAILED! <<<\n\n");
    return 1;
}
