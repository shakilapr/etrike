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
use etrike_protocol::RawFrame;

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

/// Assemble inputs for this tick from GPIO and any pending frames.
///
/// Frames here are already `verify()`-ed by the HAL layer; request frames are
/// gated by `StreamValidity` before reaching this point on target.
pub fn assemble_inputs<H: SysHal>(hal: &mut H) -> SysInputs {
    let mut inputs = SysInputs {
        now: hal.now_ms(),
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
        if wire::is_estop(&frame) {
            inputs.can_estop = true;
        } else if let Some(auto) = wire::decode_mode_request(&frame) {
            inputs.host_auto_request = auto;
        } else if let Some(source) = wire::decode_reset_request(&frame) {
            inputs.host_reset = Some(source);
        }
    }
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
pub fn step_once<H: SysHal>(hal: &mut H, core: &mut SysCore, counter: &mut u8) -> SysOutputs {
    let inputs = assemble_inputs(hal);
    let outputs = core.step(inputs);
    emit_outputs(hal, &outputs, counter);
    outputs
}

/// The control loop. Never returns.
pub fn run<H: SysHal>(hal: &mut H, core: &mut SysCore) -> ! {
    let mut counter = 0u8;
    loop {
        let _ = step_once(hal, core, &mut counter);
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

    #[test]
    fn estop_frame_latches_and_manual_mode() {
        let mut hal = MockHal::new();
        hal.gpio.estop_pressed = true;
        let mut core = SysCore::new(SysConfig::default());
        let mut counter = 0u8;
        let outputs = step_once(&mut hal, &mut core, &mut counter);
        assert!(outputs.estop_active);
        assert_eq!(outputs.mode, Mode::Manual);
        // Mode/power commands and the 0x001 estop frame are emitted.
        assert!(hal.sent.iter().any(|frame| frame.id == 0x001));
        assert!(hal.sent.iter().any(|frame| frame.id == 0x110));
    }

    #[test]
    fn remote_reset_flows_through_wire() {
        let mut hal = MockHal::new();
        let mut core = SysCore::new(SysConfig::default());
        let mut counter = 0u8;
        // Enter ESTOP via a physical press.
        hal.gpio.estop_pressed = true;
        step_once(&mut hal, &mut core, &mut counter);
        // Release and present a valid remote reset.
        hal.gpio.estop_pressed = false;
        let reset = wire::decode_reset_request(
            &RawFrame::from_slice(etrike_protocol::Bus::Low, 0x114, &[0x01, 0x52, 0x53, 0x07])
                .unwrap(),
        )
        .unwrap();
        assert!(matches!(reset, ResetSource::Remote { .. }));
        hal.frames.push(
            RawFrame::from_slice(etrike_protocol::Bus::Low, 0x114, &[0x01, 0x52, 0x53, 0x07])
                .unwrap(),
        );
        let outputs = step_once(&mut hal, &mut core, &mut counter);
        assert!(!outputs.estop_active);
    }
}
