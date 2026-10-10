//! RT firmware shell (phase P4 scaffold).
//!
//! Platform-independent shell logic for the RT node: honours SYS authority
//! (asymmetric ESTOP clear) before emitting any motion, and withholds motion
//! otherwise. The esp-hal binding lives behind the `hal` feature; everything
//! here is host-testable with a mock HAL.
//!
//! Steering, the MCP2515 High bus, and the gateway land here next; the shared
//! decision logic will live in `etrike_core::rt`.
#![cfg_attr(not(test), no_std)]

use etrike_core::rt::{wire, AuthorityGate};
use etrike_protocol::{generated, RawFrame};

/// Hardware abstraction for RT. Implemented by the esp-hal binding on target.
pub trait RtHal {
    fn now_ms(&mut self) -> u64;
    fn poll_frame(&mut self) -> Option<RawFrame>;
    fn send_frame(&mut self, frame: &RawFrame);
    fn wait_next_tick(&mut self);
}

/// Mutable RT shell state.
#[derive(Debug, Clone, Copy, Default)]
pub struct RtState {
    pub gate: AuthorityGate,
    pub desired_speed_mmps: i32,
    pub desired_gear: u8,
}

/// Run one control iteration: update authority from SYS frames, capture the
/// host drive request, and emit a drive command (zeroed when unauthorised).
pub fn step_once<H: RtHal>(hal: &mut H, state: &mut RtState) {
    let _now = hal.now_ms();
    let mut safety_valid = false;
    let mut mode_valid = false;
    let mut estop = false;

    while let Some(frame) = hal.poll_frame() {
        if let Some(active) = wire::decode_safety_estop(&frame) {
            safety_valid = true;
            estop = active;
        } else if let Some(_auto) = wire::decode_mode_auto(&frame) {
            mode_valid = true;
        } else if frame.id == generated::HostDriveCmd::ID {
            if let Ok(msg) = generated::HostDriveCmd::unpack(frame.payload()) {
                state.desired_speed_mmps = msg.speed_mmps;
                state.desired_gear = msg.gear;
            }
        }
    }

    let allowed = state.gate.observe(safety_valid, mode_valid, estop);
    let (speed, gear) = if allowed {
        (state.desired_speed_mmps, state.desired_gear)
    } else {
        (0, 0)
    };
    if let Ok(frame) = wire::encode_drive_cmd(speed, gear) {
        hal.send_frame(&frame);
    }
}

/// The control loop. Never returns.
pub fn run<H: RtHal>(hal: &mut H, state: &mut RtState) -> ! {
    loop {
        step_once(hal, state);
        hal.wait_next_tick();
    }
}

#[cfg(target_arch = "xtensa")]
pub mod hal;

#[cfg(test)]
mod tests {
    use super::*;

    struct MockHal {
        now: u64,
        frames: Vec<RawFrame>,
        sent: Vec<RawFrame>,
    }

    impl MockHal {
        fn new() -> Self {
            Self {
                now: 0,
                frames: Vec::new(),
                sent: Vec::new(),
            }
        }
    }

    impl RtHal for MockHal {
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
        fn wait_next_tick(&mut self) {
            self.now += 10;
        }
    }

    fn drive(hal: &MockHal) -> [u8; 5] {
        let frame = hal.sent.last().unwrap();
        let mut out = [0u8; 5];
        out.copy_from_slice(frame.payload());
        out
    }

    fn host_drive(hal: &mut MockHal, speed: i32, gear: u8) {
        let message = generated::HostDriveCmd {
            speed_mmps: speed,
            yaw_rate_mrad_s: 0,
            gear,
        };
        let mut frame = RawFrame::new(etrike_protocol::Bus::High, generated::HostDriveCmd::ID);
        frame.dlc = generated::HostDriveCmd::DLC as u8;
        assert_eq!(
            message.pack(&mut frame.data[..generated::HostDriveCmd::DLC]),
            etrike_protocol::CodecStatus::Ok
        );
        hal.frames.push(frame);
    }

    fn sys_clear(hal: &mut MockHal) {
        hal.frames.push(
            RawFrame::from_slice(etrike_protocol::Bus::Low, 0x011, &[0, 0, 0, 0, 0]).unwrap(),
        );
        hal.frames
            .push(RawFrame::from_slice(etrike_protocol::Bus::Low, 0x110, &[1, 1]).unwrap());
    }

    #[test]
    fn no_authority_zeroes_motion() {
        let mut hal = MockHal::new();
        let mut state = RtState::default();
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0, 0, 0, 0, 0]);
    }

    #[test]
    fn authority_after_two_clean_frames() {
        let mut hal = MockHal::new();
        let mut state = RtState::default();
        host_drive(&mut hal, 1000, 1);

        sys_clear(&mut hal);
        step_once(&mut hal, &mut state); // 1 clean frame -> still gated
        assert_eq!(drive(&hal), [0, 0, 0, 0, 0]);

        sys_clear(&mut hal);
        step_once(&mut hal, &mut state); // 2 clean frames -> authorised
                                         // 1000 mm/s, gear 1 -> 0x000003E8, gear 0x01.
        assert_eq!(drive(&hal), [0x00, 0x00, 0x03, 0xe8, 0x01]);
    }

    #[test]
    fn estop_withdraws_authority() {
        let mut hal = MockHal::new();
        let mut state = RtState::default();
        host_drive(&mut hal, 1000, 1);
        sys_clear(&mut hal);
        step_once(&mut hal, &mut state);
        sys_clear(&mut hal);
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0x00, 0x00, 0x03, 0xe8, 0x01]);

        // ESTOP frame.
        hal.frames.push(
            RawFrame::from_slice(etrike_protocol::Bus::Low, 0x011, &[2, 0, 0, 0, 0]).unwrap(),
        );
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0, 0, 0, 0, 0]);
    }
}
