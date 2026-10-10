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
use etrike_protocol::{generated, verify, CodecStatus, RawFrame};

/// Hardware abstraction for RT. Implemented by the esp-hal binding on target.
pub trait RtHal {
    fn now_ms(&mut self) -> u64;
    fn poll_frame(&mut self) -> Option<RawFrame>;
    fn send_frame(&mut self, frame: &RawFrame);
    fn wait_next_tick(&mut self);
}

/// Stream freshness windows (ms). The SYS safety stream is 200 ms and the mode
/// stream 100 ms; a generous multiple tolerates jitter and a single miss.
const SAFETY_FRESH_MS: u64 = 600;
const MODE_FRESH_MS: u64 = 300;

/// If no frame arrives within the window, the stream is treated as lost.
fn is_fresh(last: Option<u64>, now: u64, window_ms: u64) -> bool {
    last.map(|seen| now.wrapping_sub(seen) <= window_ms)
        .unwrap_or(false)
}

/// Mutable RT shell state.
#[derive(Debug, Clone, Copy, Default)]
pub struct RtState {
    gate: AuthorityGate,
    desired_speed_mmps: i32,
    desired_gear: u8,
    safety_last: Option<u64>,
    safety_estop: bool,
    mode_last: Option<u64>,
    /// Bench safety bypass: always allow motion and skip frame verification.
    pub bypass: bool,
}

impl RtState {
    pub const fn new(bypass: bool) -> Self {
        Self {
            gate: AuthorityGate::new(),
            desired_speed_mmps: 0,
            desired_gear: 0,
            safety_last: None,
            safety_estop: false,
            mode_last: None,
            bypass,
        }
    }
}

/// Run one control iteration: update authority from SYS frames, capture the
/// host drive request, and emit a drive command (zeroed when unauthorised).
pub fn step_once<H: RtHal>(hal: &mut H, state: &mut RtState) {
    let now = hal.now_ms();

    while let Some(frame) = hal.poll_frame() {
        // Only frames that match the wire contract may influence the core
        // (unless the bench safety bypass is active).
        if !state.bypass && verify(&frame) != CodecStatus::Ok {
            continue;
        }
        if let Some(active) = wire::decode_safety_estop(&frame) {
            state.safety_last = Some(now);
            state.safety_estop = active;
        } else if let Some(_auto) = wire::decode_mode_auto(&frame) {
            state.mode_last = Some(now);
        } else if frame.id == generated::HostDriveCmd::ID {
            if let Ok(msg) = generated::HostDriveCmd::unpack(frame.payload()) {
                state.desired_speed_mmps = msg.speed_mmps;
                state.desired_gear = msg.gear;
            }
        }
    }

    // Authority requires *fresh* safety and mode streams, not merely a frame on
    // this exact tick (otherwise authority would flap at the control rate).
    // Under bypass, motion is always allowed.
    let safety_valid = is_fresh(state.safety_last, now, SAFETY_FRESH_MS);
    let mode_valid = is_fresh(state.mode_last, now, MODE_FRESH_MS);
    let allowed = state.bypass
        || state
            .gate
            .observe(safety_valid, mode_valid, state.safety_estop);
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

    fn safety(estop_source: u8) -> RawFrame {
        let mut data = [estop_source, 0, 0, 0, 0];
        data[4] = etrike_protocol::e2e::sys_safety_sts_crc(&data);
        RawFrame::from_slice(etrike_protocol::Bus::Low, 0x011, &data).unwrap()
    }

    fn sys_clear(hal: &mut MockHal) {
        hal.frames.push(safety(0));
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
        hal.frames.push(safety(2));
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0, 0, 0, 0, 0]);
    }

    #[test]
    fn bypass_allows_motion_without_authority() {
        let mut hal = MockHal::new();
        let mut state = RtState::new(true);
        host_drive(&mut hal, 1000, 1);
        // No SYS frames at all, but bypass still emits the commanded motion.
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0x00, 0x00, 0x03, 0xe8, 0x01]);
    }

    #[test]
    fn authority_survives_between_frames_and_expires_when_stale() {
        let mut hal = MockHal::new();
        let mut state = RtState::default();
        host_drive(&mut hal, 1000, 1);
        sys_clear(&mut hal);
        step_once(&mut hal, &mut state);
        sys_clear(&mut hal);
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0x00, 0x00, 0x03, 0xe8, 0x01]);

        // A tick with no new frames but within the freshness window keeps
        // authority (this is the anti-flap property).
        hal.now = 100;
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0x00, 0x00, 0x03, 0xe8, 0x01]);

        // Once the window elapses the stream is stale and authority is lost.
        hal.now = 10_000;
        step_once(&mut hal, &mut state);
        assert_eq!(drive(&hal), [0, 0, 0, 0, 0]);
    }
}
