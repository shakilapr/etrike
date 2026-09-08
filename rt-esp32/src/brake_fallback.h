#pragma once
// SEB brake-ownership emergency fallback state machine (issue #3).
//
// Single-normal-producer model:
//   SYS is the SOLE normal 0x7B9 producer. RT expresses brake intent over
//   0x205; SYS converts it to the final 0x7B9 and always transmits in normal
//   operation. RT does NOT transmit 0x7B9 in NORMAL or SYS_DEGRADED.
//
// Emergency fallback (the ONLY case RT transmits 0x7B9):
//   SYS heartbeat loss is NOT proof the SYS brake task died (separate tasks).
//   RT therefore only becomes the emergency writer when the SYS 0x7B9 command
//   has ALSO actually disappeared from the Low bus for a guard interval. Even
//   then this is labelled an *emergency fallback writer* — not strict single
//   ownership — and the handback is latched and epoch-guarded.
//
// Issue #5: brake-channel health is watched INDEPENDENTLY of the heartbeat.
// Because 0x7FE and 0x7B9 are produced by different SYS tasks, a live heartbeat
// does NOT prove the SYS brake task is alive. A stale SYS 0x7B9 stream (absent
// > guard while armed) escalates straight to EMERGENCY_FALLBACK even if the
// heartbeat is still fresh — otherwise a dead SYS brake task with a live
// heartbeat would leave nobody commanding the brake.
//
// States:
//   NORMAL             SYS 0x7FE + 0x7B9 healthy. RT never TXs 0x7B9.
//   SYS_DEGRADED       SYS 0x7FE lost (motion already prohibited by caller),
//                      SYS 0x7B9 still fresh. RT observes the brake stream.
//   EMERGENCY_FALLBACK SYS 0x7B9 absent > guard — with or without 0x7FE. RT
//                      asserts 0x001 (caller) and transmits max-brake 0x7B9.
//
// Startup acquisition: the fallback path is disarmed until a valid SYS 0x7B9
// has been observed once OR the boot grace elapses — so RT booting ahead of
// SYS cannot mistake the initial absence for a SYS failure.
//
// Handback (latched): once in EMERGENCY_FALLBACK, recovery to NORMAL requires
//   SYS 0x7FE healthy again -> RT stops its own 0x7B9 TX -> clears the
//   observed-0x7B9 timestamp / opens a new epoch -> requires kSebHandbackVerifyFrames
//   consecutive FRESH 0x7B9 frames received AFTER the epoch (so RT's own
//   loopback cannot satisfy it). The vehicle remains ESTOP-latched until SYS
//   publishes its two-frame 0x011 clear (handled by the existing latch path).

#include <cstdint>

namespace rt {

enum class SebBrakeState : uint8_t {
    NORMAL = 0,
    SYS_DEGRADED = 1,
    EMERGENCY_FALLBACK = 2,
};

struct SebFallbackInput {
    int64_t now_us = 0;
    bool    sys_hb_fresh = false;   // SYS heartbeat 0x7FE fresh
    bool    sys_0x7B9_observed = false;  // a 0x7B9 was received in this update window
    bool    startup_grace_active = false; // overall boot grace
};

struct SebFallbackOutput {
    SebBrakeState state = SebBrakeState::NORMAL;
    bool emergency_tx_0x7B9 = false;  // RT should transmit max-brake 0x7B9 now
    bool emergency_0x001 = false;     // RT should assert 0x001 (rate-limited upstream)
};

class SebBrakeFallback {
public:
    SebFallbackOutput update(const SebFallbackInput& in) {
        SebFallbackOutput out;
        const int64_t now = in.now_us;

        // ── State transition logic ────────────────────────────────
        switch (state_) {
        case SebBrakeState::NORMAL:
            // Track SYS 0x7B9 observation for startup acquisition and for the
            // independent brake-channel-health monitor (issue #5).
            if (in.sys_0x7B9_observed) {
                first_sys_0x7B9_us_ = now;
                last_sys_0x7B9_seen_us_ = now;
            }
            if (in.startup_grace_active) {
                // Still within the global boot grace: no SYS dependency yet.
                arm_ = false;
            } else if (arm_ == false) {
                // Arm once we have either seen a SYS 0x7B9 or the arm grace passed.
                const bool saw_sys = (first_sys_0x7B9_us_ >= 0);
                if (saw_sys || (now - boot_us_ >= int64_t(rt::kSebFallbackArmGraceMs) * 1000)) {
                    arm_ = true;
                    armed_at_us_ = now;
                }
            }
            if (arm_) {
                // Issue #5: brake-channel health is INDEPENDENT of the SYS
                // heartbeat. A live 0x7FE is produced by SYS's heartbeat task and
                // does NOT prove the SYS *brake* task is alive. If the SYS 0x7B9
                // stream is absent beyond the guard — with OR without a heartbeat —
                // RT must become the emergency brake writer; nobody else is
                // commanding the brake.
                // The guard is measured from the last observed 0x7B9, OR — if SYS
                // has never published one — from the moment we armed, so a slow
                // cold-start SYS is given the full guard window before RT escalates
                // (it passes through SYS_DEGRADED first, matching issue #3).
                const int64_t brake_ref = (last_sys_0x7B9_seen_us_ >= 0)
                    ? last_sys_0x7B9_seen_us_ : armed_at_us_;
                const bool brake_stale = (brake_ref < 0)
                    || (now - brake_ref) >= int64_t(rt::kSebFallbackGuardMs) * 1000;
                if (brake_stale) {
                    state_ = SebBrakeState::EMERGENCY_FALLBACK;
                    handback_epoch_us_ = -1;   // fresh epoch on entry
                    handback_verify_count_ = 0;
                    break;
                }
                if (!in.sys_hb_fresh) {
                    // Heartbeat lost but the brake stream is still fresh: SYS still
                    // owns the brake. Enter SYS_DEGRADED to observe; motion is
                    // already prohibited by the caller (run_safety_checks).
                    state_ = SebBrakeState::SYS_DEGRADED;
                    degraded_since_us_ = now;
                }
            }
            break;

        case SebBrakeState::SYS_DEGRADED:
            if (in.sys_0x7B9_observed) last_sys_0x7B9_seen_us_ = now;
            if (in.sys_hb_fresh) {
                // SYS heartbeat recovered while its 0x7B9 was still present:
                // SYS never lost brake ownership — return to NORMAL directly.
                state_ = SebBrakeState::NORMAL;
                break;
            }
            // SYS 0x7B9 absent beyond the guard -> the brake producer is truly gone.
            // Measure the guard from the last observed 0x7B9, or from arm time if
            // SYS has never published one (so a cold-start SYS gets the full window).
            {
                const int64_t brake_ref = (last_sys_0x7B9_seen_us_ >= 0)
                    ? last_sys_0x7B9_seen_us_ : armed_at_us_;
                if (brake_ref < 0
                    || (now - brake_ref) >= int64_t(rt::kSebFallbackGuardMs) * 1000) {
                    state_ = SebBrakeState::EMERGENCY_FALLBACK;
                    handback_epoch_us_ = -1;   // fresh epoch on entry
                    handback_verify_count_ = 0;
                }
            }
            break;

        case SebBrakeState::EMERGENCY_FALLBACK:
            // Latched emergency behavior. Recovery is epoch-guarded: RT stops its
            // own 0x7B9 (caller), opens a fresh observation epoch, and only counts
            // 0x7B9 frames received AFTER the epoch as SYS re-ownership (so RT's
            // own loopback, or frames from before the stop, cannot satisfy it).
            if (in.sys_hb_fresh) {
                if (handback_epoch_us_ < 0) {
                    // Begin handback: stop RT 0x7B9 TX (caller reads emergency_tx)
                    // and open a fresh epoch. The frame seen this cycle is ambiguous
                    // (could be RT's own pre-stop echo) so it is NOT counted.
                    handback_epoch_us_ = now;
                    handback_verify_count_ = 0;
                } else if (in.sys_0x7B9_observed && now >= handback_epoch_us_) {
                    if (++handback_verify_count_ >= rt::kSebHandbackVerifyFrames) {
                        state_ = SebBrakeState::NORMAL;
                        handback_epoch_us_ = -1;
                        handback_verify_count_ = 0;
                    }
                }
            } else {
                // SYS heartbeat gone again (or never returned): remain in emergency
                // fallback. Reset any partial handback progress.
                handback_epoch_us_ = -1;
                handback_verify_count_ = 0;
            }
            break;
        }

        // ── Outputs for the current state ─────────────────────────
        if (state_ == SebBrakeState::EMERGENCY_FALLBACK) {
            out.state = SebBrakeState::EMERGENCY_FALLBACK;
            out.emergency_tx_0x7B9 = true;
            // 0x001 assertion is rate-limited by the caller (can_send_estop()).
            out.emergency_0x001 = true;
        } else if (state_ == SebBrakeState::SYS_DEGRADED) {
            out.state = SebBrakeState::SYS_DEGRADED;
            out.emergency_tx_0x7B9 = false;  // SYS may still own the brake
            out.emergency_0x001 = false;
        } else {
            out.state = SebBrakeState::NORMAL;
        }
        return out;
    }

    // (Re)initialise. `boot_us` anchors the startup-acquisition arm grace.
    void init(int64_t boot_us) {
        boot_us_ = boot_us;
        state_ = SebBrakeState::NORMAL;
        arm_ = false;
        armed_at_us_ = -1;
        first_sys_0x7B9_us_ = -1;
        last_sys_0x7B9_seen_us_ = -1;
        degraded_since_us_ = -1;
        handback_epoch_us_ = -1;
        handback_verify_count_ = 0;
    }

    SebBrakeState state() const { return state_; }

private:
    int64_t boot_us_ = 0;
    bool    arm_ = false;               // fallback armed (startup acquisition met)
    int64_t armed_at_us_ = -1;          // timestamp arm_ first became true
    SebBrakeState state_ = SebBrakeState::NORMAL;
    int64_t first_sys_0x7B9_us_ = -1;
    int64_t last_sys_0x7B9_seen_us_ = -1;
    int64_t degraded_since_us_ = -1;
    int64_t handback_epoch_us_ = -1;
    int     handback_verify_count_ = 0;
};

}  // namespace rt
