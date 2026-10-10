//! SYS firmware shell.
//!
//! Platform-independent shell logic for the SYS node. The hardware binding
//! (esp-hal TWAI + GPIO + WS2812) lives behind the `hal` feature and the
//! [`SysHal`] trait; everything here is host-testable with a mock HAL.
//!
//! The shell is deliberately thin: it assembles [`SysInputs`] from verified
//! frames and debounced GPIO, calls [`SysCore::step`] (the single owner of all
//! decision state), and emits [`SysOutputs`] back to the bus and pins.
#![cfg_attr(not(test), no_std)]

use etrike_core::shared::Power;
use etrike_core::sys::{wire, SysCore, SysInputs, SysOutputs};
use etrike_protocol::{generated, verify, CodecStatus, RawFrame};

/// Debounced hardware input state.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct GpioState {
    pub estop_pressed: bool,
    pub start_run: bool,
    pub brake_lever: bool,
    pub mode_long_press: bool,
    pub switch_left: bool,
    pub switch_right: bool,
    pub switch_head: bool,
}

/// Hardware abstraction the shell drives. Implemented by the esp-hal binding on
/// target and by a mock in tests.
pub trait SysHal {
    /// Monotonic milliseconds since boot.
    fn now_ms(&mut self) -> u64;
    /// Pop the next received frame, if any.
    fn poll_frame(&mut self) -> Option<RawFrame>;
    /// Send a frame on the Low bus.
    fn send_frame(&mut self, frame: &RawFrame);
    /// Read debounced hardware inputs.
    fn gpio(&mut self) -> GpioState;
    /// Block until the next control tick.
    fn wait_next_tick(&mut self);
}

/// Freshness windows (ms) for supervised peer streams.
const RT_HB_FRESH_MS: u64 = 600;
const MTR_FBK_FRESH_MS: u64 = 300;

/// True when `last` is present and within `window_ms` of `now`.
///
/// A stream that has never been seen is not treated as lost until the startup
/// window elapses, so a peer that boots slightly later does not immediately
/// latch ESTOP (mirrors the C++ startup-acquire behaviour).
fn is_fresh(last: Option<u64>, now: u64, window_ms: u64) -> bool {
    match last {
        Some(seen) => now.wrapping_sub(seen) <= window_ms,
        None => now <= window_ms,
    }
}

/// Persistent shell state for stream supervision (not decision state).
#[derive(Debug, Clone, Copy, Default)]
pub struct SysShellState {
    pub bypass: bool,
    rt_hb_last: Option<u64>,
    mtr_fbk_last: Option<u64>,
}

impl SysShellState {
    pub const fn new(bypass: bool) -> Self {
        Self {
            bypass,
            rt_hb_last: None,
            mtr_fbk_last: None,
        }
    }
}

/// Assemble inputs for this tick from GPIO and any pending frames.
///
/// Only frames that pass [`verify`] reach the core. Peer liveness (RT heartbeat,
/// MTR feedback) is tracked with freshness windows so a silent stream is treated
/// as lost rather than as present.
pub fn assemble_inputs<H: SysHal>(hal: &mut H, state: &mut SysShellState) -> SysInputs {
    let now = hal.now_ms();
    let mut inputs = SysInputs {
        now,
        ..SysInputs::default()
    };
    let gpio = hal.gpio();
    inputs.hw_estop_pressed = gpio.estop_pressed;
    inputs.hw_start_run = gpio.start_run;
    inputs.hw_brake_lever = gpio.brake_lever;
    inputs.hw_mode_long_press = gpio.mode_long_press;
    inputs.light_switches = etrike_core::sys::LightSwitches {
        left: gpio.switch_left,
        right: gpio.switch_right,
        head: gpio.switch_head,
    };

    while let Some(frame) = hal.poll_frame() {
        // Only frames that match the wire contract may influence the core
        // (unless the bench safety bypass is active).
        if !state.bypass && verify(&frame) != CodecStatus::Ok {
            continue;
        }
        if frame.id == generated::RtHeartbeat::ID {
            state.rt_hb_last = Some(now);
        } else if frame.id == generated::MtrMotorFbk::ID {
            state.mtr_fbk_last = Some(now);
        } else if wire::is_estop(&frame) {
            inputs.can_estop = true;
        } else if let Some(auto) = wire::decode_mode_request(&frame) {
            inputs.host_auto_request = auto;
        } else if let Some(source) = wire::decode_reset_request(&frame) {
            inputs.host_reset = Some(source);
        }
    }

    inputs.rt_heartbeat_ok = state.bypass || is_fresh(state.rt_hb_last, now, RT_HB_FRESH_MS);
    inputs.mtr_fbk_ok = state.bypass || is_fresh(state.mtr_fbk_last, now, MTR_FBK_FRESH_MS);
    inputs
}

/// Emit the core's outputs onto the bus.
pub fn emit_outputs<H: SysHal>(hal: &mut H, outputs: &SysOutputs, counter: &mut u8) {
    if let Ok(frame) = wire::encode_mode_cmd(wire::mode_is_auto(outputs.mode), *counter) {
        hal.send_frame(&frame);
    }
    if let Ok(frame) = wire::encode_pwr_cmd(matches!(outputs.power, Power::On), *counter) {
        hal.send_frame(&frame);
    }
    if outputs.estop_active {
        hal.send_frame(&wire::encode_estop());
    }
    *counter = counter.wrapping_add(1);
}

/// Run one control iteration. Used by [`run`] and by tests.
pub fn step_once<H: SysHal>(
    hal: &mut H,
    core: &mut SysCore,
    counter: &mut u8,
    state: &mut SysShellState,
) -> SysOutputs {
    let inputs = assemble_inputs(hal, state);
    let outputs = core.step(inputs);
    emit_outputs(hal, &outputs, counter);
    outputs
}

/// The control loop. Never returns. `bypass` enables bench pass-through.
pub fn run<H: SysHal>(hal: &mut H, core: &mut SysCore, bypass: bool) -> ! {
    let mut counter = 0u8;
    let mut state = SysShellState::new(bypass);
    loop {
        let _ = step_once(hal, core, &mut counter, &mut state);
        hal.wait_next_tick();
    }
}

#[cfg(target_arch = "xtensa")]
pub mod hal;

#[cfg(test)]
mod tests {
    use super::*;
    use etrike_core::shared::{Mode, ResetSource};
    use etrike_core::sys::SysConfig;
    use etrike_protocol::Bus;

    struct MockHal {
        now: u64,
        frames: Vec<RawFrame>,
        sent: Vec<RawFrame>,
        gpio: GpioState,
    }

    impl MockHal {
        fn new() -> Self {
            Self {
                now: 0,
                frames: Vec::new(),
                sent: Vec::new(),
                gpio: GpioState::default(),
            }
        }
    }

    impl SysHal for MockHal {
        fn now_ms(&mut self) -> u64 {
            self.now
        }
        fn poll_frame(&mut self) -> Option<RawFrame> {
            if self.frames.is_empty() {
                None
            } else {
                Some(self.frames.remove(0))
            }
        }
        fn send_frame(&mut self, frame: &RawFrame) {
            self.sent.push(*frame);
        }
        fn gpio(&mut self) -> GpioState {
            self.gpio
        }
        fn wait_next_tick(&mut self) {
            self.now += 10;
        }
    }

    fn heartbeat() -> RawFrame {
        RawFrame::from_slice(Bus::Low, 0x7FD, &[0, 0]).unwrap()
    }

    fn mtr_fbk() -> RawFrame {
        RawFrame::from_slice(Bus::Low, 0x206, &[0, 0, 0, 0]).unwrap()
    }

    #[test]
    fn estop_frame_latches_and_manual_mode() {
        let mut hal = MockHal::new();
        hal.gpio.estop_pressed = true;
        let mut core = SysCore::new(SysConfig::default());
        let mut counter = 0u8;
        let mut state = SysShellState::default();
        let outputs = step_once(&mut hal, &mut core, &mut counter, &mut state);
        assert!(outputs.estop_active);
        assert_eq!(outputs.mode, Mode::Manual);
        // Mode/power commands and the 0x001 estop frame are emitted.
        assert!(hal.sent.iter().any(|frame| frame.id == 0x001));
        assert!(hal.sent.iter().any(|frame| frame.id == 0x110));
    }

    #[test]
    fn unverified_frames_are_ignored() {
        let mut hal = MockHal::new();
        // 0x111 with the wrong DLC (1, not 2) must not reach the core.
        hal.frames
            .push(RawFrame::from_slice(Bus::Low, 0x111, &[0x01]).unwrap());
        let mut state = SysShellState::default();
        let inputs = assemble_inputs(&mut hal, &mut state);
        assert!(!inputs.host_auto_request);
    }

    #[test]
    fn bypass_ignores_estop_and_verification() {
        let mut hal = MockHal::new();
        hal.gpio.estop_pressed = true;
        // A malformed frame must also be accepted under bypass.
        hal.frames
            .push(RawFrame::from_slice(Bus::Low, 0x111, &[0x01]).unwrap());
        let mut core = SysCore::new(SysConfig {
            bypass: true,
            ..SysConfig::default()
        });
        let mut counter = 0u8;
        let mut state = SysShellState::new(true);
        let outputs = step_once(&mut hal, &mut core, &mut counter, &mut state);
        assert!(!outputs.estop_active);
    }

    #[test]
    fn startup_grace_avoids_premature_estop() {
        let mut hal = MockHal::new();
        let mut core = SysCore::new(SysConfig::default());
        let mut counter = 0u8;
        let mut state = SysShellState::default();
        // At boot with no heartbeat yet, SYS holds instead of latching ESTOP.
        let outputs = step_once(&mut hal, &mut core, &mut counter, &mut state);
        assert!(!outputs.estop_active);
    }

    #[test]
    fn missing_heartbeat_latches_estop() {
        let mut hal = MockHal::new();
        let mut core = SysCore::new(SysConfig::default());
        let mut counter = 0u8;
        let mut state = SysShellState::default();
        // Past the startup window with no 0x7FD -> RT is treated as lost.
        hal.now = 10_000;
        let outputs = step_once(&mut hal, &mut core, &mut counter, &mut state);
        assert!(outputs.estop_active);
    }

    #[test]
    fn remote_reset_flows_through_wire() {
        let mut hal = MockHal::new();
        let mut core = SysCore::new(SysConfig::default());
        let mut counter = 0u8;
        let mut state = SysShellState::default();
        // Fresh RT heartbeat + MTR feedback so neither is a reset blocker.
        hal.frames.push(heartbeat());
        hal.frames.push(mtr_fbk());
        hal.gpio.estop_pressed = true;
        step_once(&mut hal, &mut core, &mut counter, &mut state);

        // Release and present a valid remote reset with fresh streams.
        hal.gpio.estop_pressed = false;
        hal.frames.push(heartbeat());
        hal.frames.push(mtr_fbk());
        let reset = wire::decode_reset_request(
            &RawFrame::from_slice(Bus::Low, 0x114, &[0x01, 0x52, 0x53, 0x07]).unwrap(),
        )
        .unwrap();
        assert!(matches!(reset, ResetSource::Remote { .. }));
        hal.frames
            .push(RawFrame::from_slice(Bus::Low, 0x114, &[0x01, 0x52, 0x53, 0x07]).unwrap());
        let outputs = step_once(&mut hal, &mut core, &mut counter, &mut state);
        assert!(!outputs.estop_active);
    }
}
