//! SYS pure core: deterministic mode, ESTOP, and body-control logic.
//!
//! Deliberately small. One owner (`SysCore::step`), a two-mode authority model,
//! a tiny reset blocker set, and an optional **safety bypass** for bench work
//! where every command is passed straight through.

pub mod brake;
pub mod lights;
pub mod mode;
pub mod wire;

use crate::shared::{
    Authority, Clamp, EstopLatch, EstopReason, InhibitModel, InhibitReason, LatchedFault, Mode,
    Power, ResetBlocker, ResetBlockers, ResetEvidence, ResetOutcome, ResetSource,
};

pub use brake::{BrakeCommand, BrakeSource};
pub use lights::{LightBits, LightInputs, LightOutputs, LightSwitches};
pub use mode::{resolve_authority, ModeInputs};

/// Static configuration for a SYS core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SysConfig {
    pub allow_auto: bool,
    pub moving_speed_threshold_mmps: i32,
    pub mtr_fbk_recover_frames: u32,
    /// Safety bypass: when set, every command passes straight through (no ESTOP,
    /// no clamps, no reset blockers). Intended for bench work only.
    pub bypass: bool,
}

impl Default for SysConfig {
    fn default() -> Self {
        Self {
            allow_auto: true,
            moving_speed_threshold_mmps: 50,
            mtr_fbk_recover_frames: 3,
            bypass: false,
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
    pub seb_l3_fault: bool,
    pub rt_brake_intent_kpa: Option<u16>,
    // requests / faults
    pub host_auto_request: bool,
    /// Host power request (`0x112`); `None` when none has been received.
    pub host_power: Option<bool>,
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
            seb_l3_fault: false,
            rt_brake_intent_kpa: None,
            host_auto_request: false,
            host_power: None,
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
    pub heartbeat: u8,
    pub reset_outcome: Option<ResetOutcome>,
}

/// The SYS decision core.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SysCore {
    config: SysConfig,
    estop: EstopLatch,
    inhibit: InhibitModel,
    mtr_fbk_healthy_frames: u32,
    heartbeat: u8,
}

impl SysCore {
    pub fn new(config: SysConfig) -> Self {
        Self {
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
        if self.config.bypass {
            return self.step_bypass(&inputs);
        }

        // 1. Latched faults and transient inhibits.
        if inputs.seb_l3_fault {
            self.inhibit.latch(LatchedFault::SebL3);
        }
        self.update_mtr_fbk_inhibit(inputs.mtr_fbk_ok);

        // 2. ESTOP entry (first reason wins while active).
        if !self.estop.is_active() {
            if let Some(reason) = self.first_estop_reason(&inputs) {
                self.estop.enter(reason);
            }
        }

        // 3. Reset transaction (before authority so a reset restores mode now).
        let reset_outcome = self.process_reset(&inputs);

        // 4. Resolve authority.
        let authority = resolve_authority(ModeInputs {
            estop_active: self.estop.is_active(),
            latched_fault: self.inhibit.latched_any(),
            host_auto_request: inputs.host_auto_request,
            physical_run: inputs.hw_start_run,
            allow_auto: self.config.allow_auto,
        });

        // 5. Brake arbitration.
        let brake = brake::arbitrate(
            self.estop.is_active(),
            inputs.hw_brake_lever,
            inputs.rt_brake_intent_kpa,
        );

        // 6. Body lights.
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
            heartbeat: self.heartbeat,
            reset_outcome,
        }
    }

    /// Bypass path: pass everything straight through, ignoring safety entirely.
    /// Power defaults ON (bench), and the host can switch it off via `0x112`.
    fn step_bypass(&mut self, inputs: &SysInputs) -> SysOutputs {
        let authority = Authority {
            mode: if inputs.host_auto_request {
                Mode::Auto
            } else {
                Mode::Manual
            },
            power: match inputs.host_power {
                Some(false) => Power::Off,
                _ => Power::On,
            },
            clamp: Clamp::None,
        };
        let brake = brake::arbitrate(false, inputs.hw_brake_lever, inputs.rt_brake_intent_kpa);
        let lights = lights::evaluate(
            authority.mode,
            LightInputs {
                switches: inputs.light_switches,
                lever: inputs.hw_brake_lever,
                estop: false,
                host: inputs.host_lights,
            },
        );
        self.heartbeat = self.heartbeat.wrapping_add(1);
        SysOutputs {
            mode: authority.mode,
            power: authority.power,
            estop_active: false,
            estop_reason: None,
            brake,
            lights,
            heartbeat: self.heartbeat,
            reset_outcome: None,
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

    /// Compute the (small) reset blocker set.
    fn compute_blockers(&self, source: ResetSource, inputs: &SysInputs) -> ResetBlockers {
        let mut blockers = ResetBlockers::empty();
        if inputs.hw_estop_pressed {
            blockers.add(ResetBlocker::PhysicalEstop);
        }
        if inputs.measured_speed_mmps.unsigned_abs()
            > self.config.moving_speed_threshold_mmps.unsigned_abs()
        {
            blockers.add(ResetBlocker::Moving);
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

        let mut i = input(0);
        i.hw_estop_pressed = true;
        let out = core.step(i);
        assert!(out.estop_active);
        assert_eq!(out.estop_reason, Some(EstopReason::HwButton));
        assert_eq!(out.mode, Mode::Manual);

        // Button still pressed -> reset rejected.
        let mut i = input(10);
        i.hw_estop_pressed = true;
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert_eq!(
            out.reset_outcome,
            Some(ResetOutcome::Rejected(ResetBlocker::PhysicalEstop))
        );
        assert!(out.estop_active);

        // Released, stationary -> accepted.
        let mut i = input(20);
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
    fn reset_restores_mode_on_the_same_tick() {
        let mut core = core();
        let mut i = input(0);
        i.rt_heartbeat_ok = false;
        i.hw_start_run = true;
        core.step(i);

        let mut i = input(10);
        i.rt_heartbeat_ok = true;
        i.hw_start_run = true;
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert!(matches!(
            out.reset_outcome,
            Some(ResetOutcome::Cleared { .. })
        ));
        assert!(!out.estop_active);
        assert_eq!(out.mode, Mode::Manual);
        assert_eq!(out.power, Power::On);
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
    fn reset_rejected_at_extreme_speed() {
        // i32::MIN must not overflow (regression for the former .abs() call).
        let mut core = core();
        let mut i = input(0);
        i.hw_estop_pressed = true;
        core.step(i);
        let mut i = input(10);
        i.measured_speed_mmps = i32::MIN;
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert_eq!(
            out.reset_outcome,
            Some(ResetOutcome::Rejected(ResetBlocker::Moving))
        );
    }

    #[test]
    fn seb_l3_latches_clamps_mode_and_reset_clears() {
        let mut core = core();
        let mut i = input(0);
        i.seb_l3_fault = true;
        core.step(i);

        // Latched fault clamps to MANUAL/off.
        let mut i = input(10);
        i.host_reset = Some(ResetSource::ModeLongPress);
        let out = core.step(i);
        assert_eq!(out.mode, Mode::Manual);
        assert_eq!(out.power, Power::Off);
        assert!(matches!(
            out.reset_outcome,
            Some(ResetOutcome::Cleared { .. })
        ));
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
    fn bypass_passes_everything_through() {
        let mut core = SysCore::new(SysConfig {
            bypass: true,
            ..SysConfig::default()
        });
        let mut i = input(0);
        i.hw_estop_pressed = true; // ignored in bypass
        i.rt_heartbeat_ok = false; // ignored in bypass
        i.host_auto_request = true;
        i.hw_start_run = true;
        let out = core.step(i);
        assert!(!out.estop_active);
        assert_eq!(out.mode, Mode::Auto);
        assert_eq!(out.power, Power::On);
    }

    #[test]
    fn bypass_powers_on_by_default_and_host_can_switch_off() {
        let mut core = SysCore::new(SysConfig {
            bypass: true,
            ..SysConfig::default()
        });
        // No run latch and no host request still powers on (bench pass-through).
        let out = core.step(input(0));
        assert_eq!(out.power, Power::On);

        let mut i = input(10);
        i.host_power = Some(false);
        let out = core.step(i);
        assert_eq!(out.power, Power::Off);
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
        assert!(!core.inhibit.transient_any());
    }

    /// Walk a pseudo-random input sequence and check the safety invariants.
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
            if out.estop_active {
                assert_eq!(out.mode, Mode::Manual);
                assert_eq!(out.power, Power::Off);
                assert_eq!(out.brake.source, BrakeSource::Estop);
            }
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
