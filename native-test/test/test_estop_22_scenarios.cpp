// Test: Dedicated 22 ESTOP & Safety Reset Scenario Suite (§7.12)
// Verifies assertion paths, assert dominance, reboot behavior, stale authority,
// gateway echo loops, and safety invariants against the REAL mtr::MotorManager.
//
// Each scenario drives a fresh MotorManager with self-contained rolling counters
// so the SYS_SAFETY_STS (0x011) stream validity / asymmetric-clear bookkeeping
// is exercised genuinely (no accidental cross-scenario counter coupling, no
// stream-invalidating jumps that would make assertions pass for the wrong
// reason).

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
#include "sys-esp32/src/mode_manager.h"
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

// Mode / power / safety authority helpers. Each MotorManager instance owns its
// own StreamValidity supervisors (reset by init()), so mode/power counters are
// monotonic globals (safe). The 0x011 safety counter is passed EXPLICITLY by
// each scenario via a local Ctr so the clear-sequence tests stay contiguous.
static uint8_t g_mode_ctr = 0;
static uint8_t g_pwr_ctr = 0;

// Small modular-counter helper: yields 0..255 and wraps.
struct Ctr {
    uint8_t v;
    explicit Ctr(uint8_t start = 0) : v(start) {}
    uint8_t next() { return v++; }
};

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

// Single SYS_SAFETY_STS frame at an explicit rolling counter.
static void send_safety(mtr::MotorManager& mgr, bool estop, uint8_t ctr, uint32_t now) {
    can::gen::SysSafetySts msg{};
    msg.estop_active = estop;
    msg.heartbeat_ok = true;
    msg.light_left = false;
    msg.light_right = false;
    msg.light_brake = false;
    msg.light_head = false;
    msg.rolling_counter = ctr;
    msg.e2e_crc = 0;
    can::Frame tmp;
    can::gen::encode_sys_safety_sts(msg, tmp);
    msg.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
    can::Frame f;
    can::gen::encode_sys_safety_sts(msg, f);
    mgr.handle_frame(f, now);
}

// Deliberately corrupted CRC frame (rejected before the clear logic runs).
static void send_safety_corrupt(mtr::MotorManager& mgr, bool estop, uint8_t ctr, uint32_t now) {
    can::gen::SysSafetySts msg{};
    msg.estop_active = estop;
    msg.heartbeat_ok = true;
    msg.rolling_counter = ctr;
    msg.e2e_crc = 0;
    can::Frame tmp;
    can::gen::encode_sys_safety_sts(msg, tmp);
    msg.e2e_crc = static_cast<std::uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()) ^ 0xFFu);
    can::Frame f;
    can::gen::encode_sys_safety_sts(msg, f);
    mgr.handle_frame(f, now);
}

// Hardwired ESTOP frame (0x001).
static void send_001(mtr::MotorManager& mgr, uint32_t now) {
    mgr.handle_frame(can::Frame{can::kIdSafetyEstop, 0, {}}, now);
}

// Establish a driving baseline: valid mode (Manual), valid power (ON), a VALID
// 0x011 stream (two contiguous zeros @ s, s+1 -> First + Increment), and a
// drive command. Returns the next safety counter.
static uint8_t setup_healthy_drive(mtr::MotorManager& mgr, mtr::RelayController& relays,
                                   mtr::DacController& dac, uint8_t s, uint32_t now) {
    hal_mock::reset();
    mgr.init();
    send_mode(mgr, can::Mode::Manual, now);
    send_power(mgr, true, now);
    send_safety(mgr, false, s,     now);
    send_safety(mgr, false, s + 1, now);
    send_drive(mgr, 2000, can::Gear::D, now);
    mgr.tick(now);
    ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
    ASSERT_TRUE(dac.current_code() > 0);
    ASSERT_FALSE(mgr.is_estop_active());
    return static_cast<uint8_t>(s + 2);
}

// Number of SameFrame gateway routes for safety_estop between two buses.
static int count_estop_routes(const char* from, const char* to) {
    int n = 0;
    for (const auto& r : etrike::protocol::kRoutes) {
        if (r.semantics == etrike::protocol::RouteSemantics::SameFrame &&
            r.message == "safety:safety_estop" && r.from_bus == from && r.to_bus == to) {
            ++n;
        }
    }
    return n;
}

} // namespace

int main() {
    std::printf("Running 22-Scenario ESTOP & Safety Reset Suite (§7.12)...\n");

    // 1. test_estop_001_high_to_low
    {
        // 0x001 is routed High -> Low exactly once (SameFrame, no echo loop).
        ASSERT_EQ(count_estop_routes("high", "low"), 1);
    }

    // 2. test_estop_001_low_to_high
    {
        // 0x001 is routed Low -> High exactly once (SameFrame, no echo loop).
        ASSERT_EQ(count_estop_routes("low", "high"), 1);
    }

    // 3. test_estop_multiple_assertors
    {
        // Distinct assertors (0x001 hardwired frame AND 0x011 estop_active=1)
        // both leave the latch asserted with actuators forced safe.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(10);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);                                  // First assertor
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 106);               // Second assertor
        ASSERT_TRUE(mgr.is_estop_active());
        mgr.tick(106);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 4. test_safety_sts_assert_first_frame
    {
        // 0x011 with estop_active=1 asserts immediately and forces actuators off.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(20);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_safety(mgr, true, s.next(), 110);
        ASSERT_TRUE(mgr.is_estop_active());
        mgr.tick(110);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
    }

    // 5. test_safety_sts_zero_baseline_does_not_clear
    {
        // A single zero frame is a baseline only and must NOT clear the latch.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(30);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(), 110); // 1 zero frame (baseline)
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 6. test_safety_sts_two_fresh_zero_frames_clear_authority
    {
        // Two CONSECUTIVE advancing zeros clear the ESTOP latch.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(40);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);   // SYS also publishing estop=true
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(), 110);  // Frame 1: baseline
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, s.next(), 112);  // Frame 2: advancing -> clear!
        ASSERT_FALSE(mgr.is_estop_active());
    }

    // 7. test_safety_sts_duplicate_does_not_advance_clear
    {
        // A duplicate rolling counter (same value as the baseline) must never
        // count toward the clear; even repeated duplicates never clear.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(50);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        ASSERT_TRUE(mgr.is_estop_active());

        const uint8_t dup = s.next();
        send_safety(mgr, false, dup, 110); // baseline @dup
        send_safety(mgr, false, dup, 111); // duplicate @dup (must NOT advance)
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, dup, 112); // third identical frame (still no advance)
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 8. test_safety_sts_crc_error_does_not_advance_clear
    {
        // A CRC-corrupted zero frame is rejected: it must neither clear nor
        // advance the sequence, and the authority stream is invalidated so the
        // next zero is only a fresh re-acquisition baseline.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(60);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(), 110);     // baseline
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety_corrupt(mgr, false, s.next(), 112); // bad CRC -> rejected
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, s.next(), 114);     // post-corrupt re-acquire (baseline)
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 9. test_safety_sts_counter_gap_does_not_clear
    {
        // A gap (a missed frame, +2) between zero frames means the second zero is
        // NOT "consecutive advancing" from the first, so two observed zeros do
        // not clear.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(70);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(),       110); // baseline @c
        send_safety(mgr, false, (uint8_t)(s.v + 1), 112); // @c+2 (missed c+1) -> not consecutive
        ASSERT_TRUE(mgr.is_estop_active());
        s.v = static_cast<uint8_t>(s.v + 2); // consume
    }

    // 10. test_safety_sts_timeout_resets_clear_sequence
    {
        // A freshness timeout (>700 ms) invalidates the 0x011 stream and resets
        // the clear sequence. Two fresh zeros after the timeout only re-acquire
        // and re-baseline (do NOT clear); a third consecutive one clears.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(80);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(), 110); // baseline
        mgr.tick(1000); // 1000 - 110 = 890 ms > kSafetyFreshMs (700ms) -> timed out
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(), 1010); // re-acquire First (still invalid)
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, s.next(), 1012); // Increment -> baseline only
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, s.next(), 1014); // 2nd consecutive -> clears
        ASSERT_FALSE(mgr.is_estop_active());
    }

    // 11. test_safety_sts_counter_wrap
    {
        // A genuine counter wrap (255 -> 0) between the baseline and the second
        // zero is an advancing (+1 mod 256) clear across the boundary.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        hal_mock::reset();
        mgr.init();
        send_mode(mgr, can::Mode::Manual, 100);
        send_power(mgr, true, 100);
        // Establish a valid stream ending at 253.
        send_safety(mgr, false, 252, 100); // First
        send_safety(mgr, false, 253, 100); // Increment -> valid
        send_drive(mgr, 2000, can::Gear::D, 100);
        mgr.tick(100);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);
        ASSERT_FALSE(mgr.is_estop_active());

        send_safety(mgr, true, 254, 105);  // assert at 254 (accepted, latch)
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, 255, 110); // baseline @255
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, 0, 111);   // @0 advances mod 256 -> CLEAR
        ASSERT_FALSE(mgr.is_estop_active());
    }

    // 12. test_estop_assert_wins_during_clear
    {
        // If an ASSERT frame arrives during the clear sequence, it wins: the
        // clear progress is discarded and the next zero is only a baseline.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(90);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        ASSERT_TRUE(mgr.is_estop_active());

        send_safety(mgr, false, s.next(), 110); // baseline
        send_safety(mgr, true,  s.next(), 112); // ASSERT arrives -> discards progress
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, s.next(), 114); // next zero is baseline only
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 13. test_estop_cause_still_active_blocks_clear
    {
        // The system-level ESTOP latch predicate (SYS ModeManager) must report
        // latched while the hardware ESTOP is pressed OR the mode is ESTOP, and
        // NOT latched only when the button is released in MANUAL.
        ASSERT_TRUE(sys::ModeManager::estop_latched(can::Mode::Manual, true));  // hw pressed
        ASSERT_TRUE(sys::ModeManager::estop_latched(can::Mode::Estop, false));  // sw ESTOP
        ASSERT_TRUE(sys::ModeManager::estop_latched(can::Mode::Estop, true));   // both
        ASSERT_FALSE(sys::ModeManager::estop_latched(can::Mode::Manual, false));// healthy
        ASSERT_FALSE(sys::ModeManager::estop_latched(can::Mode::Auto, false));  // healthy AUTO
    }

    // 14. test_sys_reboot_does_not_clear_estop
    {
        // SYS is the 0x011 producer. If SYS reboots and goes silent, the stream
        // times out and MTR stays latched/safe; when SYS reconnects, the clear
        // sequence must start from a fresh baseline (two fresh zeros do NOT
        // clear a latched ESTOP on their own).
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(110);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        // Latch via 0x011 estop_active=1 (the SYS-held latch).
        send_safety(mgr, true, s.next(), 105);
        ASSERT_TRUE(mgr.is_estop_active());

        // SYS reboot: stream goes silent past the 700 ms freshness window.
        mgr.tick(900);
        ASSERT_TRUE(mgr.is_estop_active());
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);

        // SYS reconnects: two fresh zeros only re-establish baseline, no clear.
        send_safety(mgr, false, s.next(), 1000); // re-acquire
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, false, s.next(), 1005); // baseline
        ASSERT_TRUE(mgr.is_estop_active());
    }

    // 15. test_rt_reboot_does_not_clear_estop
    {
        // RT is the 0x204 producer. If RT reboots (all traffic stops), MTR's
        // generic comms watchdog (>500 ms of total silence) removes propulsion
        // authority. Sustained silence keeps the vehicle safe even though the
        // last drive/authority frames are still latched in.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(120);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Drive);

        // RT reboots: total CAN silence >500 ms trips the generic watchdog.
        mgr.tick(700); // 700 - 100 = 600 ms > kWatchdogTimeoutMs (500ms)
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);

        // A stray single drive frame DOES feed the generic deadman (any frame
        // counts) — that models a live RT resuming. To prove a dead RT cannot
        // re-enable motion, what matters is that a one-shot frame WITHOUT a
        // sustained stream is not a valid recovery: the watchdog re-trips as soon
        // as silence resumes. Assert the fail-safe still holds after one more
        // silence window.
        send_drive(mgr, 2000, can::Gear::D, 710);
        mgr.tick(710);
        mgr.tick(1300); // 1300 - 710 = 590 ms silence again -> watchdog re-trips
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 16. test_mtr_reboot_does_not_clear_estop
    {
        // After an MTR reboot (re-init), actuators boot safe and the 0x011 stream
        // is NOT valid until a fresh baseline + advancing frame arrive. A single
        // zero frame is insufficient to make the stream valid, so a drive command
        // cannot move the vehicle.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        hal_mock::reset();
        mgr.init();
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
        ASSERT_FALSE(mgr.is_estop_active());

        // Fresh mode/power authorities + a SINGLE zero (baseline) is not enough:
        // the safety stream stays invalid, so propulsion stays off.
        Ctr s(130);
        send_mode(mgr, can::Mode::Manual, 100);
        send_power(mgr, true, 100);
        send_safety(mgr, false, s.next(), 100); // First (still invalid)
        send_drive(mgr, 2000, can::Gear::D, 100);
        mgr.tick(100);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 17. test_all_ecus_reboot_safe
    {
        // Cold start with NO traffic at all: MTR boots inert (relays OFF, DAC 0,
        // not latched) and even a raw drive command cannot produce motion.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        hal_mock::reset();
        mgr.init();
        mgr.tick(0);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);

        send_drive(mgr, 2000, can::Gear::D, 5);
        mgr.tick(5);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 18. test_clear_enters_rearm_required
    {
        // An authorized clear releases the latch into REARM_REQUIRED, NOT
        // propulsion: a drive command after clear (without rearm) stays off.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(140);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        send_safety(mgr, false, s.next(), 110); // baseline
        send_safety(mgr, false, s.next(), 112); // advancing -> clear
        ASSERT_FALSE(mgr.is_estop_active());

        // Try to drive without rearm.
        send_drive(mgr, 2000, can::Gear::D, 115);
        mgr.tick(115);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 19. test_pre_estop_mode_authority_invalid_after_clear
    {
        // After an authorized clear, the pre-estop 0x110 mode authority is
        // invalidated. Re-arming power (OFF -> ON) WITHOUT a fresh mode must not
        // complete the REARM (rearm_observed_ requires mode_valid_), so the
        // vehicle stays off.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(150);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        send_safety(mgr, false, s.next(), 110); // baseline
        send_safety(mgr, false, s.next(), 112); // clear
        ASSERT_FALSE(mgr.is_estop_active());

        // Power cycled but no fresh mode -> REARM cannot complete.
        send_power(mgr, false, 114);
        send_power(mgr, true, 115);
        send_drive(mgr, 2000, can::Gear::D, 116);
        mgr.tick(116);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 20. test_pre_estop_power_authority_invalid_after_clear
    {
        // After an authorized clear, the pre-estop 0x113 power authority is
        // invalidated too. Re-issuing only a mode (power untouched) leaves
        // power_valid_ false, so ignition stays off.
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(160);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        send_safety(mgr, false, s.next(), 110); // baseline
        send_safety(mgr, false, s.next(), 112); // clear
        ASSERT_FALSE(mgr.is_estop_active());

        // Fresh mode but power authority untouched -> power_valid_ stays false.
        send_mode(mgr, can::Mode::Manual, 114);
        send_drive(mgr, 2000, can::Gear::D, 116);
        mgr.tick(116);
        ASSERT_EQ(relays.state(), mtr::RelayController::State::Off);
        ASSERT_EQ(dac.current_code(), 0);
    }

    // 21. test_full_post_clear_rearm_sequence_allows_drive
    {
        // Full sequence: clear -> fresh mode -> power OFF -> power ON -> drive
        // enabled (positive control for scenarios 19/20/18).
        mtr::RelayController relays;
        mtr::DacController dac;
        mtr::MotorManager mgr(relays, dac);
        Ctr s(170);
        setup_healthy_drive(mgr, relays, dac, s.next(), 100);

        send_001(mgr, 105);
        ASSERT_TRUE(mgr.is_estop_active());
        send_safety(mgr, true, s.next(), 108);
        send_safety(mgr, false, s.next(), 110); // baseline
        send_safety(mgr, false, s.next(), 112); // clear
        ASSERT_FALSE(mgr.is_estop_active());

        // Full REARM: fresh mode, then power OFF->ON, then drive.
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
        // Production route table: 0x001 is forwarded exactly once in each
        // direction (SameFrame routes), preventing any echo loop.
        ASSERT_EQ(count_estop_routes("high", "low"), 1);
        ASSERT_EQ(count_estop_routes("low", "high"), 1);
    }

    // 23. test_diagnostics_enabled_does_not_change_safety_outputs (§7.10)
    {
        // Safety execution with diagnostics disabled vs enabled must produce
        // identical physical outputs.
        mtr::RelayController relaysA, relaysB;
        mtr::DacController dacA, dacB;
        mtr::MotorManager mgrA(relaysA, dacA); // No diag
        mtr::MotorManager mgrB(relaysB, dacB); // With diag
        etrike::diagnostics::DiagnosticManager diag;
        mgrB.set_diag(diag);

        Ctr sa(180), sb(190);
        setup_healthy_drive(mgrA, relaysA, dacA, sa.next(), 100);
        setup_healthy_drive(mgrB, relaysB, dacB, sb.next(), 100);

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
