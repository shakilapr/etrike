#pragma once
// MTR STM32G431 ? Motor Actuation Supervisor & Telemetry Engine
// Handles speed mapping to DAC, relay coordination, ESTOP monitoring, and CAN TX.

#include <cstdint>
#include <cmath>
#include <algorithm>
#include "config.h"
#include "relay_controller.h"
#include "dac_controller.h"
#include "protocol/compat/can.hpp"
#include "shared_config.h"
#include "stream_validity.h"
#include "shared/diagnostics.h"

namespace mtr {

class MotorManager {
public:
    MotorManager(RelayController& relays, DacController& dac)
        : relays_(relays), dac_(dac) {}

    // Wire the shared DiagnosticManager (Phase B reporting). Optional: if unset,
    // no diagnostic reports are emitted. Bookkeeping only ? never changes reaction.
    void set_diag(etrike::diagnostics::DiagnosticManager& diag) { diag_ = &diag; }

    void init() {
        relays_.init();
        dac_.init();
        estop_active_ = false;
        comms_timed_out_ = false;
        comms_healthy_ = false;
        first_frame_seen_ = false;
        target_speed_mmps_ = 0;
        target_gear_ = can::Gear::N;
        active_gear_ = can::Gear::N;
        shift_dwell_start_ms_ = 0;
        current_mode_ = can::Mode::Manual;
        mode_valid_ = false;
        power_valid_ = false;
        power_state_on_ = false;
        // Bind authority stream supervisors (Low bus, SYS as producer).
        mode_val_.set_key(1 /*low*/, can::kIdSysModeCmd, kAuthFreshMs);
        pwr_val_.set_key(1 /*low*/, can::kIdSysPwrCmd, kAuthFreshMs);
        safety_val_.set_key(1 /*low*/, can::kIdSysSafetySts, kSafetyFreshMs);
        safety_state_valid_ = false;
        clear_confirm_ = 0;
        last_estop_zero_ = false;
        clear_seq_last_ctr_ = 0;
        rearm_required_ = false;
        rearm_off_seen_ = false;
        rearm_observed_ = false;
        rearm_start_ms_ = 0;
        prev_pwr_on_ = false;
        // Dedicated 0x204 watchdog state (issue #2).
        last_drive_ms_ = 0;
        drive_seen_ = false;
        drive_expected_ = false;
        expected_since_ms_ = 0;
        drive_cmd_timed_out_ = false;
        drive_recover_count_ = 0;
        last_drive_gap_ok_ = true;
    }

    // Process incoming CAN frames
    void handle_frame(const can::Frame& frame, uint32_t now_ms) {
        last_rx_ms_ = now_ms;
        first_frame_seen_ = true;
        comms_timed_out_ = false;
        comms_healthy_ = true; // Maintain link health flag ? cleared in tick() on watchdog expiry

        switch (frame.id) {
        case can::kIdSafetyEstop: { // 0x001 DLC 0 (Explicit Emergency Stop, hardwired dump)
            trigger_estop();
            break;
        }

        case can::kIdSysSafetySts: { // 0x011 ? persistent E-stop authority (latched state)
            handle_safety_status(frame, now_ms);
            break;
        }

        case can::kIdSysModeCmd: { // 0x110 ? authoritative mode command from SYS (MANUAL/AUTO only)
            can::gen::SysModeCmd mode_cmd{};
            if (can::gen::decode_sys_mode_cmd(frame.view(), mode_cmd) == can::gen::CodecStatus::Ok) {
                const bool ok = mode_val_.observe(
                    static_cast<std::uint8_t>(mode_cmd.rolling_counter), now_ms);
                mode_valid_ = ok;
                if (!ok) break;  // stale/invalid authority: keep last mode, inhibit drive
                current_mode_ = (mode_cmd.mode ? can::Mode::Auto : can::Mode::Manual);
                // NOTE: 0x110 no longer carries ESTOP. The latched E-stop is
                // asserted/cleared solely via 0x011 SYS_SAFETY_STS.
            }
            break;
        }

        case can::kIdRtDriveCmd: { // 0x204 (motor_speed_mmps int32, gear uint8)
            can::gen::RtDriveCmd cmd{};
            if (can::gen::decode_rt_drive_cmd(frame.view(), cmd) == can::gen::CodecStatus::Ok) {
                // Mode authority lost: inhibit the drive command (no autonomous propulsion).
                if (!mode_valid_) {
                    if (diag_) diag_->raise(etrike::diagnostics::DiagId::MtrCmdStreamUnauthorised);
                    break;
                }
                target_speed_mmps_ = cmd.motor_speed_mmps;
                target_gear_ = static_cast<can::Gear>(cmd.gear);
                // ESTOP is no longer cleared by a 0x204 reset sequence; only 0x011 can.

                // Dedicated 0x204 watchdog (issue #2): a valid drive command is
                // the "heartbeat" that keeps the propulsion authority alive.
                // The trip is released ONLY by the confirmed 3-frame recovery in
                // tick(); a single valid frame must not clear the latched trip.
                last_drive_ms_ = now_ms;
                drive_seen_ = true;
                if (last_drive_gap_ok_) {
                    // Consecutive valid receive events at plausible cadence count
                    // toward confirmed recovery. 0x204 has no rolling counter, so
                    // identical payloads still count as separate valid events.
                    if (drive_recover_count_ < kDriveCmdRecoverFrames)
                        ++drive_recover_count_;
                } else {
                    drive_recover_count_ = 1;
                }
                last_drive_gap_ok_ = true;
            }
            break;
        }

        case can::kIdSysPwrCmd: { // 0x113 ? authoritative power command from SYS
            can::gen::SysPwrCmd pwr{};
            if (can::gen::decode_sys_pwr_cmd(frame.view(), pwr) == can::gen::CodecStatus::Ok) {
                const bool ok = pwr_val_.observe(
                    static_cast<std::uint8_t>(pwr.rolling_counter), now_ms);
                power_valid_ = ok;
                if (!ok) break;  // stale/invalid power authority: enter power-safe (zero propulsion)
                const bool pwr_on = (pwr.power_state != 0);
                if (!pwr_on) {
                    rearm_off_seen_ = true;
                } else if (rearm_required_ && rearm_off_seen_ && mode_valid_) {
                    rearm_observed_ = true;
                    rearm_start_ms_ = 0;   // REARM complete
                } else if (rearm_required_ && !rearm_off_seen_) {
                    // 0x113 ON arrived but the required OFF edge was never observed:
                    // REARM sequence violation. Reporting only ? reaction unchanged.
                    if (diag_) diag_->raise(etrike::diagnostics::DiagId::MtrRearmSequenceViolation);
                }
                prev_pwr_on_ = pwr_on;
                power_state_on_ = pwr_on;
                // ESTOP is no longer cleared by a 0x113 reset sequence; only 0x011 can.
            }
            break;
        }

        default:
            break;
        }
    }

    // Monitor SYS_SAFETY_STS (0x011): E2E-CRC protected persistent E-stop authority.
    void handle_safety_status(const can::Frame& frame, uint32_t now_ms) {
        can::gen::SysSafetySts msg{};
        if (can::gen::decode_sys_safety_sts(frame.view(), msg) != can::gen::CodecStatus::Ok) return;

        // E2E: CRC-8 over the protected payload bytes [0..3]; reject on mismatch.
        const std::uint8_t crc = ::etrike::protocol::e2e::sys_safety_sts_crc(frame.data.data());
        if (crc != msg.e2e_crc) {
            safety_val_.invalidate_now();
            safety_state_valid_ = false;
            clear_confirm_ = 0;
            last_estop_zero_ = false;
            if (diag_) diag_->raise(etrike::diagnostics::DiagId::MtrSysSafetyCrcError);
            return;
        }

        const bool ok = safety_val_.observe(static_cast<std::uint8_t>(msg.rolling_counter), now_ms);
        safety_state_valid_ = ok;
        if (!ok) {
            clear_confirm_ = 0;
            last_estop_zero_ = false;
            if (diag_) diag_->raise(etrike::diagnostics::DiagId::MtrSysSafetyCounterStale);
            return;
        }
        last_safety_ms_ = now_ms;

        if (msg.estop_active) {
            if (!estop_active_) trigger_estop();
            // Assert frame: reset the clear sequence (last_estop_zero_/clear_confirm_
            // were cleared by trigger_estop; keep them cleared if already latched).
            clear_confirm_ = 0;
            last_estop_zero_ = false;
            return;
        }

        // estop_active == 0.
        //
        // Asymmetric clear: an authorized clear requires TWO consecutive zero
        // frames whose rolling counters advance by exactly +1 (mod 256) from the
        // previous zero *in the clear sequence*. A duplicate, a gap (missed
        // frame), or a counter jump restarts the sequence from that frame as the
        // new baseline. Continuous zeros while NOT latched never accumulate credit
        // (normal running must never trigger a clear/REARM). Every new latch
        // (trigger_estop) also resets the sequence so a stale pre-latch zero can
        // never pair with a post-latch zero.
        if (!estop_active_) {
            // Not latched: observe counters for sequence state only, never clear.
            clear_confirm_ = 0;
            last_estop_zero_ = true;
            clear_seq_last_ctr_ = msg.rolling_counter;
            return;
        }

        const bool first_zero = !last_estop_zero_;
        const bool advances = (msg.rolling_counter ==
                               static_cast<std::uint8_t>(clear_seq_last_ctr_ + 1u));
        if (first_zero) {
            clear_confirm_ = 1;  // baseline
        } else if (advances) {
            ++clear_confirm_;    // consecutive advancing zero
        } else {
            clear_confirm_ = 1;  // duplicate/gap -> this frame is the new baseline
        }
        clear_seq_last_ctr_ = msg.rolling_counter;
        last_estop_zero_ = true;

        if (clear_confirm_ >= 2) authorized_clear(now_ms);
    }

    // Authorized E-stop clear: latch released only after the validated two-frame
    // sequence. Authority streams are invalidated so a stale 0x204/0x110/0x113
    // cannot re-enable motion until a fresh REARM sequence is observed.
    void authorized_clear(uint32_t now_ms) {
        estop_active_ = false;
        mode_valid_ = false;
        power_valid_ = false;
        mode_val_.invalidate_now();
        pwr_val_.invalidate_now();
        clear_confirm_ = 0;
        last_estop_zero_ = false;
        clear_seq_last_ctr_ = 0;
        rearm_required_ = true;
        // rearm_off_seen_ is intentionally NOT reset here: the OFF edge observed
        // during the ESTOP (0x113=OFF) carries across the clear to pair with the
        // post-clear ON. Only rearm_observed_ must be re-acquired each recovery.
        rearm_observed_ = false;
        rearm_start_ms_ = now_ms;   // anchor the REARM-timeout diagnostic (issue RC4)
    }

    // Periodic evaluation (called at 5 ms rate)
    void tick(uint32_t now_ms) {
        // 1. Check Comms Watchdog (500 ms)
        comms_timed_out_ = first_frame_seen_ && (now_ms - last_rx_ms_ > kWatchdogTimeoutMs);
        if (comms_timed_out_) comms_healthy_ = false;

        // 2. Evaluate Actuation (fail-safe on latched ESTOP, CAN timeout, lost power
        //    authority, lost/invalid safety-state stream, or un-rearmed recovery).
        // Power authority: derive ignition from a valid power command, gated by the
        // persistent safety-state stream (0x011) and the REARM sequence.
        // Freshness supervision: a silently-stopping 0x011 stream must fail safe even
        // between received frames.
        if (safety_state_valid_ && now_ms >= last_safety_ms_ &&
            (now_ms - last_safety_ms_ > kSafetyFreshMs)) {
            safety_state_valid_ = false;
            safety_val_.invalidate_now();
            clear_confirm_ = 0;
            last_estop_zero_ = false;
            if (diag_) {
                diag_->raise(etrike::diagnostics::DiagId::MtrSysSafetyStsTimeout,
                             static_cast<std::uint16_t>(now_ms - last_safety_ms_));
            }
        }
        ignition_on_ = power_valid_ && power_state_on_ && safety_state_valid_ &&
                       (!rearm_required_ || rearm_observed_);

        // 2b. Dedicated 0x204 drive-command watchdog (issue #2).
        // Armed only while drive is *expected*: AUTO authority, power ON, valid
        // mode/safety/power authority, and the REARM sequence satisfied. This is
        // deliberately independent of the generic any-frame comms watchdog so a
        // frozen 0x204 cannot keep a last throttle applied while 0x110/0x113/0x011
        // keep the generic deadman fed.
        const bool expect_drive =
            mode_valid_ && power_valid_ && safety_state_valid_ && power_state_on_ &&
            current_mode_ == can::Mode::Auto && (!rearm_required_ || rearm_observed_);
        drive_expected_ = expect_drive;

        if (drive_expected_) {
            if (expected_since_ms_ == 0) expected_since_ms_ = now_ms;
            // A gap longer than the recovery cadence window between valid 0x204
            // events breaks the "consecutive at plausible cadence" recovery chain.
            if (drive_seen_ && (now_ms - last_drive_ms_ > kDriveCmdRecoverMaxGapMs))
                last_drive_gap_ok_ = false;

            if (drive_cmd_timed_out_) {
                // Latched trip: release only via confirmed recovery ? N consecutive
                // valid 0x204 frames at plausible cadence. Restart the stale clock
                // so a subsequent silence re-trips after a full timeout.
                if (drive_recover_count_ >= kDriveCmdRecoverFrames && last_drive_gap_ok_) {
                    drive_cmd_timed_out_ = false;
                    expected_since_ms_ = now_ms;
                }
            } else {
                // Reference time is the last valid 0x204; if none has ever arrived
                // while drive is expected (dead RT drive sender at startup), the
                // arm time is used so the trip still fires.
                const uint32_t ref = drive_seen_ ? last_drive_ms_ : expected_since_ms_;
                if ((now_ms - ref) > kDriveCmdTimeoutMs) {
                    drive_cmd_timed_out_ = true;
                    if (diag_) {
                        diag_->raise(etrike::diagnostics::DiagId::MtrRtDriveCmdTimeout,
                                     static_cast<std::uint16_t>(now_ms - expected_since_ms_));
                    }
                }
            }
        } else {
            // Not expecting drive: watchdog disarmed, trip cleared, recovery reset.
            drive_cmd_timed_out_ = false;
            expected_since_ms_ = 0;
            drive_recover_count_ = 0;
            last_drive_gap_ok_ = true;
        }

        if (estop_active_ || comms_timed_out_ || drive_cmd_timed_out_ ||
            !power_valid_ || !safety_state_valid_ ||
            (rearm_required_ && !rearm_observed_)) {
            target_speed_mmps_ = 0;
            relays_.set_state(RelayController::State::Off);
            dac_.force_zero();

            // Issue RC4 (Case B): a REARM that is required but never observed
            // within kRearmTimeoutMs after the clear is surfaced as a diagnostic
            // (the vehicle stays safely inhibited). Rate-limit to one raise per
            // window by comparing against the anchor.
            if (rearm_required_ && !rearm_observed_ && rearm_start_ms_ != 0
                && (now_ms - rearm_start_ms_ >= kRearmTimeoutMs)) {
                if (diag_) {
                    diag_->raise(etrike::diagnostics::DiagId::MtrRearmSequenceViolation,
                                 static_cast<std::uint16_t>(now_ms - rearm_start_ms_));
                }
            }
            return;
        }

        // Mode authority lost: inhibit the drive command (zero propulsion) but keep power.
        if (!mode_valid_) {
            target_speed_mmps_ = 0;
        }

        // Update Relays with live ignition state and direction shift arc-protection
        bool is_direction_shift = (active_gear_ == can::Gear::D && target_gear_ == can::Gear::R) ||
                                  (active_gear_ == can::Gear::R && target_gear_ == can::Gear::D);

        if (is_direction_shift && shift_dwell_start_ms_ == 0) {
            shift_dwell_start_ms_ = now_ms;
            dac_.force_zero();
            relays_.set_gear(can::Gear::N, ignition_on_);
        }

        if (shift_dwell_start_ms_ != 0) {
            if (now_ms - shift_dwell_start_ms_ < kShiftDwellMs) {
                dac_.force_zero();
                return;
            }
            // Dwell complete, allow shift
            shift_dwell_start_ms_ = 0;
            active_gear_ = target_gear_;
        } else {
            active_gear_ = target_gear_;
        }

        relays_.set_gear(active_gear_, ignition_on_);

        if (!ignition_on_) {
            dac_.force_zero();
            return;
        }

        // Update Throttle DAC (canonical path only)
        bool drive_enabled = ignition_on_ && (active_gear_ == can::Gear::D);
        bool reverse_enabled = ignition_on_ && (active_gear_ == can::Gear::R);
        bool neutral_active = (active_gear_ == can::Gear::N) || !ignition_on_;

        // Directional setpoint sign verification:
        // In Drive (D), speed must be non-negative (0..3000 mm/s). Negative values are rejected.
        // In Reverse (R), canonical 0x204 transmits negative speed (-500..0 mm/s) or legacy positive magnitude (<=500 mm/s).
        // A forward setpoint (>500 mm/s) must never drive Reverse.
        int32_t speed_mag = 0;
        if (drive_enabled) {
            if (target_speed_mmps_ > 0) {
                speed_mag = target_speed_mmps_;
            }
        } else if (reverse_enabled) {
            if (target_speed_mmps_ < 0) {
                speed_mag = -target_speed_mmps_;
            } else if (target_speed_mmps_ > 0 && target_speed_mmps_ <= kMaxReverseSpeedMmps) {
                speed_mag = target_speed_mmps_; // Support legacy positive reverse setpoints <= 500 mm/s
            }
        }

        if (neutral_active || speed_mag == 0 || (!drive_enabled && !reverse_enabled)) {
            dac_.force_zero();
        } else {
            uint16_t code = calculate_dac_code_(speed_mag, drive_enabled, reverse_enabled);
            dac_.set_throttle(code, true);
        }
    }

    void trigger_estop() {
        estop_active_ = true;
        target_speed_mmps_ = 0;
        target_gear_ = can::Gear::N;
        active_gear_ = can::Gear::N;
        shift_dwell_start_ms_ = 0;
        relays_.set_state(RelayController::State::Off);
        dac_.force_zero();
        // Any new latch (hardwired 0x001, GPIO, or 0x011 assert) restarts the
        // asymmetric clear sequence: the next zero must establish a fresh baseline.
        clear_confirm_ = 0;
        last_estop_zero_ = false;
    }

    // Generate 0x120 SYS_THROTTLE_STS (100 Hz)
    can::Frame build_throttle_status_frame() const {
        can::gen::SysThrottleSts sts{};
        bool inhibited = propulsion_inhibited();
        sts.speed_mmps = inhibited ? 0 : static_cast<int16_t>(target_speed_mmps_);
        can::Frame fr;
        can::gen::encode_sys_throttle_sts(sts, fr);
        return fr;
    }

    // Generate 0x206 MTR_MOTOR_FBK (50 Hz)
    can::Frame build_motor_feedback_frame() const {
        can::gen::MtrMotorFbk fbk{};
        bool inhibited = propulsion_inhibited();
        fbk.motor_command_speed_mmps = inhibited ? 0 : static_cast<int16_t>(target_speed_mmps_);
        fbk.gear_state = static_cast<uint8_t>(relays_.current_gear());

        uint8_t flags = 0;
        if (estop_active_) {
            flags |= shared::kMtrFaultEstopActive; // Redundant ESTOP ACK to SYS (Gap #15)
        }
        if (comms_timed_out_ || drive_cmd_timed_out_) {
            flags |= shared::kMtrFaultCmdTimeout; // Command timeout flag (0x02)
        }
        flags |= shared::kMtrFaultStartupReady; // Bit 4
        fbk.fault_flags = flags;

        can::Frame fr;
        can::gen::encode_mtr_motor_fbk(fbk, fr);
        return fr;
    }

    bool is_estop_active() const { return estop_active_; }
    bool is_comms_timed_out() const { return comms_timed_out_; }
    bool is_drive_cmd_timed_out() const { return drive_cmd_timed_out_; }
    bool is_drive_expected() const { return drive_expected_; }
    bool is_comms_healthy() const { return comms_healthy_; }
    int32_t target_speed_mmps() const { return target_speed_mmps_; }
    can::Gear target_gear() const { return target_gear_; }

    // Centralized propulsion-inhibit predicate: any of these conditions forces
    // zero propulsion ( limp / safe state ).
    bool propulsion_inhibited() const {
        return estop_active_ || comms_timed_out_ || drive_cmd_timed_out_ ||
               !power_valid_ || !safety_state_valid_ ||
               (rearm_required_ && !rearm_observed_) ||
               !ignition_on_ || (active_gear_ == can::Gear::N);
    }

private:
    uint16_t calculate_dac_code_(int32_t speed_mmps, bool forward, bool reverse) const {
        if (speed_mmps <= 0) return 0;

        // Treat speeds below the shared low-speed threshold as zero to avoid
        // requesting the 0.8 V motor idle voltage (kDacMinCode) for tiny setpoints,
        // which would produce deadband jitter or no motion at all.
        if (speed_mmps < static_cast<int32_t>(shared::kLowSpeedThreshMmps)) return 0;

        int32_t max_speed = forward ? kMaxForwardSpeedMmps : kMaxReverseSpeedMmps;
        float norm = static_cast<float>(speed_mmps) / static_cast<float>(max_speed);
        norm = std::clamp(norm, 0.0f, 1.0f);

        // Use a motion-floor slightly above kDacMinCode to clear the actuator deadband.
        // kDacMinCode (655 = 0.8 V) is the idle threshold; true motion begins at ~0.85 V.
        static constexpr uint16_t kDacActiveFloor = 700; // ~0.855 V
        float code_f = static_cast<float>(kDacActiveFloor)
                     + norm * static_cast<float>(kDacMaxCode - kDacActiveFloor);
        return std::clamp(static_cast<uint16_t>(code_f), kDacMinCode, kDacMaxCode);
    }

    RelayController& relays_;
    DacController& dac_;
    etrike::diagnostics::DiagnosticManager* diag_{nullptr};  // Phase B reporting (optional)

    bool estop_active_{false};
    bool comms_timed_out_{false};
    bool comms_healthy_{false};
    bool first_frame_seen_{false};
    uint32_t last_rx_ms_{0};

    int32_t target_speed_mmps_{0};
    can::Gear target_gear_{can::Gear::N};
    can::Gear active_gear_{can::Gear::N};
    can::Mode current_mode_{can::Mode::Manual};
    bool ignition_on_{false};

    // Actuator-authority validity (from SYS 0x110/0x113 rolling-counter streams).
    // mode_valid_ false -> inhibit 0x204 drive command (zero propulsion).
    // power_valid_ false -> power-safe (zero propulsion, relays off).
    bool mode_valid_{false};
    bool power_valid_{false};
    bool power_state_on_{false};
    etrike::protocol::StreamValidity mode_val_;
    etrike::protocol::StreamValidity pwr_val_;
    static constexpr uint32_t kAuthFreshMs = can::gen::SysModeCmd::kCycleMs * 5;  // 100ms cycle -> 500ms

    // Persistent safety-state stream (0x011 SYS_SAFETY_STS) supervision.
    // FTTI-derived freshness timeout; deliberately NOT a multiple of the 200 ms
    // cycle so a silent clock-doubling fault cannot keep the stream "fresh".
    static constexpr uint32_t kSafetyFreshMs = 700;
    etrike::protocol::StreamValidity safety_val_;
    bool safety_state_valid_{false};
    uint32_t last_safety_ms_{0};
    // Asymmetric assert/clear bookkeeping for the 0x011 authority.
    uint8_t clear_confirm_{0};
    bool last_estop_zero_{false};
    uint8_t clear_seq_last_ctr_{0};
    // REARM: after an authorized clear, propulsion stays inhibited until a fresh
    // 0x113 OFF->ON edge (with a fresh 0x110 seen meanwhile) is observed.
    bool rearm_required_{false};
    bool rearm_off_seen_{false};
    bool rearm_observed_{false};
    // Issue RC4 (Case B): when a REARM is required but the fresh 0x113 OFF->ON
    // edge never arrives (power stayed ON through the clear in a non-SYS flow),
    // propulsion stays inhibited silently. rearm_start_ms_ anchors a timeout that
    // surfaces this as a diagnostic instead of a silent stuck state.
    uint32_t rearm_start_ms_{0};
    // If REARM is not observed within this window after an authorized clear,
    // raise MtrRearmSequenceViolation (reporting only; safe default is unchanged:
    // the vehicle stays inhibited until the operator cycles power / re-drives 0x113).
    static constexpr uint32_t kRearmTimeoutMs = 10'000;
    bool prev_pwr_on_{false};

    static constexpr uint32_t kShiftDwellMs{50};
    uint32_t shift_dwell_start_ms_{0};

    // ?? Dedicated 0x204 drive-command watchdog state (issue #2) ??
    uint32_t last_drive_ms_{0};          // last valid 0x204 receive time (ms)
    bool     drive_seen_{false};         // has any valid 0x204 ever arrived
    bool     drive_expected_{false};     // computed each tick: AUTO+power+valid authority
    uint32_t expected_since_ms_{0};      // when drive_expected first became true (arm clock)
    bool     drive_cmd_timed_out_{false};// latched trip ? 0x204 stale while expected
    uint8_t  drive_recover_count_{0};    // consecutive valid 0x204 events (confirmed recovery)
    bool     last_drive_gap_ok_{true};   // inter-arrival gaps stayed within cadence window
};

}  // namespace mtr
