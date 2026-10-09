//! SYS pure core: deterministic safety, mode, and body-control logic.
//!
//! `SysCore::step` is the single mutation path for all decision state. Inputs
//! are plain values (already verified by the shell); outputs are plain values
//! the shell serialises. Because the core is the sole owner of its state and
//! processes inputs in a fixed order, the logic is deterministic and free of
//! the multi-writer races present in the C++ firmware.

pub mod brake;
pub mod lights;
pub mod mode;
pub mod mtr_ack;

use crate::shared::{
    Authority, EstopLatch, EstopReason, InhibitModel, InhibitReason, LatchedFault, Mode, Power,
    ResetBlocker, ResetBlockers, ResetEvidence, ResetOutcome, ResetSource,
};

pub use brake::{BrakeCommand, BrakeSource};
pub use lights::{LightBits, LightInputs, LightOutputs, LightSwitches};
pub use mode::{resolve_authority, ModeInputs};
pub use mtr_ack::{AckState, MtrAckWatchdog};

/// Static configuration for a SYS core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SysConfig {
    pub allow_auto: bool,
    pub moving_speed_threshold_mmps: i32,
    pub mtr_ack_timeout_ms: u64,
    pub mtr_ack_max_retries: u32,
    pub mtr_fbk_recover_frames: u32,
}

impl Default for SysConfig {
    fn default() -> Self {
        Self {
            allow_auto: true,
            moving_speed_threshold_mmps: 50,
            mtr_ack_timeout_ms: 100,
            mtr_ack_max_retries: 3,
            mtr_fbk_recover_frames: 3,
        }
    }
}

/// Everything the shell feeds the core in one tick.
#[derive(Debug, Clone, Copy)]
pub struct SysInputs {
    pub now: u64,
    // hardware
    pub hw_estop_pressed: bool,
    pub hw_start_run: bool,
    pub hw_brake_lever: bool,
    pub hw_mode_long_press: bool,
    pub light_switches: LightSwitches,
    // peers / measurements
    pub measured_speed_mmps: i32,
    pub rt_heartbeat_ok: bool,
    pub mtr_fbk_ok: bool,
    pub mtr_estop_ack: bool,
    pub seb_l3_fault: bool,
    pub rt_brake_intent_kpa: Option<u16>,
    // requests / faults
    pub host_auto_request: bool,
    pub host_reset: Option<ResetSource>,
    pub host_lights: Option<LightBits>,
    pub can_estop: bool,
    pub task_deadline_missed: bool,
    pub can_bus_off: bool,
}

impl Default for SysInputs {
    fn default() -> Self {
        Self {
            now: 0,
            hw_estop_pressed: false,
            hw_start_run: false,
            hw_brake_lever: false,
            hw_mode_long_press: false,
            light_switches: LightSwitches::default(),
            measured_speed_mmps: 0,
            rt_heartbeat_ok: true,
            mtr_fbk_ok: true,
            mtr_estop_ack: true,
            seb_l3_fault: false,
            rt_brake_intent_kpa: None,
            host_auto_request: false,
            host_reset: None,
            host_lights: None,
            can_estop: false,
            task_deadline_missed: false,
            can_bus_off: false,
        }
    }
}

/// Everything the shell sends from the core in one tick.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SysOutputs {
    pub mode: Mode,
    pub power: Power,
    pub estop_active: bool,
    pub estop_reason: Option<EstopReason>,
    pub brake: BrakeCommand,
    pub lights: LightOutputs,
    pub mtr_ack: AckState,
    pub heartbeat: u8,
    pub reset_outcome: Option<ResetOutcome>,
}

/// The SYS decision core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SysCore {
    config: SysConfig,
    estop: EstopLatch,
    inhibit: InhibitModel,
    mtr_ack: MtrAckWatchdog,
    mtr_fbk_healthy_frames: u32,
    heartbeat: u8,
}

impl SysCore {
    pub fn new(config: SysConfig) -> Self {
        Self {
            mtr_ack: MtrAckWatchdog::new(config.mtr_ack_timeout_ms, config.mtr_ack_max_retries),
            config,
            estop: EstopLatch::new(),
            inhibit: InhibitModel::new(),
            mtr_fbk_healthy_frames: 0,
            heartbeat: 0,
        }
    }

    pub const fn estop(&self) -> &EstopLatch {
        &self.estop
    }

    /// Advance the core by one tick.
    pub fn step(&mut self, inputs: SysInputs) -> SysOutputs {
        // 1. Latched faults and transient inhibits (faults dominate the tick).
        if inputs.seb_l3_fault {
            self.inhibit.latch(LatchedFault::SebL3);
        }
        self.update_mtr_fbk_inhibit(inputs.mtr_fbk_ok);

        // 2. ESTOP entry (faults first; first reason wins while active).
        if !self.estop.is_active() {
            if let Some(reason) = self.first_estop_reason(&inputs) {
                self.estop.enter(reason);
            }
        }

        // 3. MTR ESTOP acknowledgement watchdog.
        if self.estop.is_active() && matches!(self.mtr_ack.state(), AckState::Idle) {
            self.mtr_ack.arm(inputs.now);
        }
        if inputs.mtr_estop_ack {
            self.mtr_ack.on_ack();
        }
        let ack = self.mtr_ack.tick(inputs.now);
        if ack == AckState::Exhausted {
            self.inhibit.latch(LatchedFault::MtrEstopAck);
        }

        // 4. Resolve authority (mode/power/clamp).
        let authority = self.resolve(&inputs);

        // 5. Brake arbitration.
        let brake = brake::arbitrate(
            self.estop.is_active(),
            inputs.hw_brake_lever,
            inputs.rt_brake_intent_kpa,
        );

        // 6. Validated reset transaction.
        let reset_outcome = self.process_reset(&inputs);

        // 7. Body lights.
        let lights = lights::evaluate(
            authority.mode,
            LightInputs {
                switches: inputs.light_switches,
                lever: inputs.hw_brake_lever,
                estop: self.estop.is_active(),
                host: inputs.host_lights,
            },
        );

        self.heartbeat = self.heartbeat.wrapping_add(1);

        SysOutputs {
            mode: authority.mode,
            power: authority.power,
            estop_active: self.estop.is_active(),
            estop_reason: self.estop.reason(),
            brake,
            lights,
            mtr_ack: ack,
            heartbeat: self.heartbeat,
            reset_outcome,
        }
    }

    fn first_estop_reason(&self, inputs: &SysInputs) -> Option<EstopReason> {
        if inputs.hw_estop_pressed {
            Some(EstopReason::HwButton)
        } else if inputs.can_estop {
            Some(EstopReason::Can001)
        } else if inputs.task_deadline_missed {
            Some(EstopReason::TaskDeadline)
        } else if inputs.can_bus_off {
            Some(EstopReason::CanBusoff)
        } else if !inputs.rt_heartbeat_ok {
            Some(EstopReason::RtHbLost)
        } else if self.mtr_ack.is_exhausted() {
            Some(EstopReason::MtrFault)
        } else {
            None
        }
    }

    fn update_mtr_fbk_inhibit(&mut self, mtr_fbk_ok: bool) {
        if !mtr_fbk_ok {
            self.inhibit.set_transient(InhibitReason::MtrFbkLoss);
            self.mtr_fbk_healthy_frames = 0;
        } else if self.inhibit.transient_contains(InhibitReason::MtrFbkLoss) {
            self.mtr_fbk_healthy_frames += 1;
            if self.mtr_fbk_healthy_frames >= self.config.mtr_fbk_recover_frames {
                self.inhibit.clear_transient(InhibitReason::MtrFbkLoss);
            }
        }
    }

    fn resolve(&self, inputs: &SysInputs) -> Authority {
        resolve_authority(ModeInputs {
            estop_active: self.estop.is_active(),
            latched_fault: self.inhibit.latched_any(),
            host_auto_request: inputs.host_auto_request,
            physical_run: inputs.hw_start_run,
            allow_auto: self.config.allow_auto,
        })
    }

    /// True while a latched fault's *cause* is still present. A latched fault
    /// blocks reset only while its cause is active; once the cause clears, the
    /// reset transaction clears the latch (mirrors the C++
    /// `latched_causes_currently_clearable`).
    fn latched_cause_active(&self, inputs: &SysInputs) -> bool {
        inputs.seb_l3_fault
    }

    /// Compute the reset blockers from the current state and inputs.
    fn compute_blockers(&self, source: ResetSource, inputs: &SysInputs) -> ResetBlockers {
        let mut blockers = ResetBlockers::empty();
        if inputs.hw_estop_pressed {
            blockers.add(ResetBlocker::PhysicalEstop);
        }
        if self.latched_cause_active(inputs) {
            blockers.add(ResetBlocker::LatchedFault);
        }
        if inputs.measured_speed_mmps.abs() > self.config.moving_speed_threshold_mmps {
            blockers.add(ResetBlocker::Moving);
        }
        if !inputs.rt_heartbeat_ok {
            blockers.add(ResetBlocker::HeartbeatLoss);
        }
        if matches!(self.mtr_ack.state(), AckState::Awaiting) {
            blockers.add(ResetBlocker::MtrEstopActive);
        }
        if self.inhibit.transient_any() {
            blockers.add(ResetBlocker::TransientInhibit);
        }
        if !source.is_valid() {
            blockers.add(ResetBlocker::InvalidToken);
        }
        blockers
    }

    fn process_reset(&mut self, inputs: &SysInputs) -> Option<ResetOutcome> {
        let source = inputs.host_reset.or(if inputs.hw_mode_long_press {
            Some(ResetSource::ModeLongPress)
        } else {
            None
        })?;
        let blockers = self.compute_blockers(source, inputs);
        let outcome = self.estop.try_reset(ResetEvidence { source, blockers });
        if let ResetOutcome::Cleared { .. } = outcome {
            self.inhibit.clear_latched();
            self.mtr_ack.reset();
        }
        Some(outcome)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn core() -> SysCore {
        SysCore::new(SysConfig::default())
    }

    fn input(now: u64) -> SysInputs {
        SysInputs {
            now,
            ..SysInputs::default()
        }
    }

    #[test]
    fn estop_from_button_then_release_then_reset() {
        let mut core = core();

        // Button pressed -> ESTOP.
        let mut i = input(0);
        i.hw_estop_pressed = true;
        let out = core.step(i);
        assert!(out.estop_active);
        assert_eq!(out.estop_reason, Some(EstopReason::HwButton));
        assert_eq!(out.mode, Mode::Manual);

        // Button still pressed, request reset -> rejected.
        let mut i = input(10);
        i.hw_estop_pressed = true;
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert_eq!(
            out.reset_outcome,
            Some(ResetOutcome::Rejected(ResetBlocker::PhysicalEstop))
        );
        assert!(out.estop_active);

        // Button released, MTR acknowledged, stationary -> accepted.
        let mut i = input(20);
        i.mtr_estop_ack = true;
        i.host_reset = Some(ResetSource::Remote {
            token: crate::shared::REMOTE_RESET_TOKEN_MAGIC,
            seq: 1,
        });
        let out = core.step(i);
        assert!(matches!(
            out.reset_outcome,
            Some(ResetOutcome::Cleared { .. })
        ));
        assert!(!out.estop_active);
    }

    #[test]
    fn reset_rejected_while_moving() {
        let mut core = core();
        let mut i = input(0);
        i.hw_estop_pressed = true;
        core.step(i);

        let mut i = input(10);
        i.measured_speed_mmps = 500;
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert_eq!(
            out.reset_outcome,
            Some(ResetOutcome::Rejected(ResetBlocker::Moving))
        );
    }

    #[test]
    fn seb_l3_latches_and_blocks_reset_until_cleared() {
        let mut core = core();

        let mut i = input(0);
        i.seb_l3_fault = true;
        core.step(i);

        // Latched fault clamps to MANUAL/off and blocks reset while its cause
        // is still present.
        let mut i = input(10);
        i.seb_l3_fault = true;
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert_eq!(out.mode, Mode::Manual);
        assert_eq!(out.power, Power::Off);
        assert_eq!(
            out.reset_outcome,
            Some(ResetOutcome::Rejected(ResetBlocker::LatchedFault))
        );

        // Fault cause clears; a reset now clears the latch too.
        let mut i = input(20);
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert!(matches!(
            out.reset_outcome,
            Some(ResetOutcome::Cleared { .. })
        ));
        assert_eq!(out.mode, Mode::Manual);
    }

    #[test]
    fn host_auto_ignored_under_estop() {
        let mut core = core();
        let mut i = input(0);
        i.hw_estop_pressed = true;
        i.host_auto_request = true;
        i.hw_start_run = true;
        let out = core.step(i);
        assert_eq!(out.mode, Mode::Manual);
        assert_eq!(out.power, Power::Off);
    }

    #[test]
    fn busy_rt_does_not_reset_latch() {
        // A flapping RT heartbeat must not clear a latched ESTOP by itself.
        let mut core = core();
        let mut i = input(0);
        i.rt_heartbeat_ok = false;
        let out = core.step(i);
        assert!(out.estop_active);
        assert_eq!(out.estop_reason, Some(EstopReason::RtHbLost));

        let mut i = input(10);
        i.rt_heartbeat_ok = true;
        let out = core.step(i);
        assert!(
            out.estop_active,
            "heartbeat recovery must not auto-clear ESTOP"
        );
    }

    #[test]
    fn deterministic_across_two_cores() {
        let mut a = core();
        let mut b = core();
        for step in 0..8u64 {
            let mut i = input(step * 10);
            i.hw_start_run = true;
            i.host_auto_request = step % 2 == 0;
            i.mtr_fbk_ok = step % 3 != 0;
            i.can_estop = step == 5;
            let out_a = a.step(i);
            let out_b = b.step(i);
            assert_eq!(out_a, out_b);
            assert_eq!(a, b);
        }
    }

    #[test]
    fn mtr_fbk_loss_recovers_after_frames() {
        let mut core = core();
        let mut i = input(0);
        i.mtr_fbk_ok = false;
        core.step(i);
        for step in 1..=3u64 {
            let mut i = input(step * 10);
            i.mtr_fbk_ok = true;
            core.step(i);
        }
        // After three healthy frames the inhibit has cleared.
        assert!(!core.inhibit.transient_any());
    }

    /// Walk a 2000-tick pseudo-random input sequence and check the safety
    /// invariants hold at every step.
    #[test]
    fn invariants_hold_over_a_sequence() {
        let mut core = core();
        let mut previous_estop = false;
        let mut seed = 0x1234_5678u32;
        for step in 0..2000u64 {
            seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let bit = |n: u32| seed & (1 << n) != 0;
            let mut i = input(step * 10);
            i.hw_estop_pressed = bit(0);
            i.can_estop = bit(1);
            i.host_auto_request = bit(2);
            i.hw_start_run = bit(3);
            i.rt_heartbeat_ok = bit(4);
            i.mtr_fbk_ok = bit(5);
            i.seb_l3_fault = bit(6);
            i.measured_speed_mmps = if bit(7) { 500 } else { 0 };
            i.host_reset = if bit(8) {
                Some(ResetSource::ModeLongPress)
            } else {
                None
            };

            let out = core.step(i);

            // ESTOP always forces MANUAL and power off.
            if out.estop_active {
                assert_eq!(out.mode, Mode::Manual);
                assert_eq!(out.power, Power::Off);
                assert_eq!(out.brake.source, BrakeSource::Estop);
            }
            // ESTOP can only clear through a validated reset.
            if previous_estop && !out.estop_active {
                assert!(matches!(
                    out.reset_outcome,
                    Some(ResetOutcome::Cleared { .. })
                ));
            }
            previous_estop = out.estop_active;
        }
    }
}
