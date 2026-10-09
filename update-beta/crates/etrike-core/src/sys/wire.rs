//! SYS CAN I/O mapping: frame ⇄ core types.
//!
//! Kept in the pure core so the shell only touches hardware. Encoders/decoders
//! use the generated codecs; the shell calls these instead of hand-packing
//! bytes.

use etrike_protocol::generated;
use etrike_protocol::{Bus, CodecStatus, RawFrame};

use super::{Mode, SysOutputs};
use crate::shared::{ResetBlockers, ResetSource};

/// Build a Low-bus frame with the given id and DLC.
fn low_frame(id: u32, dlc: usize) -> RawFrame {
    let mut frame = RawFrame::new(Bus::Low, id);
    frame.dlc = dlc as u8;
    frame
}

/// True when the frame is a `0x001` emergency stop (DLC 0).
pub fn is_estop(frame: &RawFrame) -> bool {
    frame.id == generated::SafetyEstop::ID && frame.dlc == generated::SafetyEstop::DLC as u8
}

/// Encode a `0x001` emergency-stop frame.
pub fn encode_estop() -> RawFrame {
    low_frame(generated::SafetyEstop::ID, generated::SafetyEstop::DLC)
}

/// Decode a host/HMI mode request (`0x111`); returns the requested AUTO flag.
pub fn decode_mode_request(frame: &RawFrame) -> Option<bool> {
    if frame.id != generated::HmiModeReq::ID {
        return None;
    }
    generated::HmiModeReq::unpack(frame.payload())
        .ok()
        .map(|msg| msg.req_mode)
}

/// Decode a host/HMI power request (`0x112`); returns the requested ON flag.
pub fn decode_power_request(frame: &RawFrame) -> Option<bool> {
    if frame.id != generated::HmiPwrReq::ID {
        return None;
    }
    generated::HmiPwrReq::unpack(frame.payload())
        .ok()
        .map(|msg| msg.req_start)
}

/// Decode an authenticated remote reset request (`0x114`).
pub fn decode_reset_request(frame: &RawFrame) -> Option<ResetSource> {
    if frame.id != generated::HostEstopResetReq::ID {
        return None;
    }
    generated::HostEstopResetReq::unpack(frame.payload())
        .ok()
        .map(|msg| ResetSource::Remote {
            token: msg.reset_token,
            seq: msg.request_seq,
        })
}

/// Encode the mode command (`0x110`).
pub fn encode_mode_cmd(auto: bool, counter: u8) -> Result<RawFrame, CodecStatus> {
    let message = generated::SysModeCmd {
        mode: auto,
        rolling_counter: counter,
    };
    let mut frame = low_frame(generated::SysModeCmd::ID, generated::SysModeCmd::DLC);
    let status = message.pack(&mut frame.data[..generated::SysModeCmd::DLC]);
    if status != CodecStatus::Ok {
        return Err(status);
    }
    Ok(frame)
}

/// Encode the power command (`0x113`).
pub fn encode_pwr_cmd(on: bool, counter: u8) -> Result<RawFrame, CodecStatus> {
    let message = generated::SysPwrCmd {
        power_state: on,
        rolling_counter: counter,
    };
    let mut frame = low_frame(generated::SysPwrCmd::ID, generated::SysPwrCmd::DLC);
    let status = message.pack(&mut frame.data[..generated::SysPwrCmd::DLC]);
    if status != CodecStatus::Ok {
        return Err(status);
    }
    Ok(frame)
}

/// Encode the reset response (`0x115`).
pub fn encode_reset_rsp(
    request_seq: u8,
    accepted: bool,
    blockers: ResetBlockers,
    counter: u8,
) -> Result<RawFrame, CodecStatus> {
    let message = generated::SysEstopResetRsp {
        request_seq,
        // Contract enum: 0 = ACCEPTED, 1 = REJECTED.
        result: !accepted,
        blocker_mask: blockers.bits() as u16,
        rolling_counter: counter,
    };
    let mut frame = low_frame(
        generated::SysEstopResetRsp::ID,
        generated::SysEstopResetRsp::DLC,
    );
    let status = message.pack(&mut frame.data[..generated::SysEstopResetRsp::DLC]);
    if status != CodecStatus::Ok {
        return Err(status);
    }
    Ok(frame)
}

/// Convenience: the mode byte to publish for a resolved [`Mode`].
pub fn mode_is_auto(mode: Mode) -> bool {
    matches!(mode, Mode::Auto)
}

/// Snapshot helper for tests / diagnostics.
pub fn summarize(outputs: &SysOutputs) -> (bool, Mode) {
    (outputs.estop_active, outputs.mode)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mode_cmd_round_trips() {
        let frame = encode_mode_cmd(true, 7).unwrap();
        assert_eq!(frame.id, 0x110);
        assert_eq!(frame.payload(), &[0x01, 0x07]);
        let decoded = generated::SysModeCmd::unpack(frame.payload()).unwrap();
        assert!(decoded.mode);
        assert_eq!(decoded.rolling_counter, 7);
    }

    #[test]
    fn power_cmd_round_trips() {
        let frame = encode_pwr_cmd(false, 5).unwrap();
        assert_eq!(frame.payload(), &[0x00, 0x05]);
    }

    #[test]
    fn decodes_canonical_reset_request() {
        // Bytes from protocol/vectors/payload-v1.json.
        let frame = RawFrame::from_slice(Bus::Low, 0x114, &[0x01, 0x52, 0x53, 0x07]).unwrap();
        assert_eq!(
            decode_reset_request(&frame),
            Some(ResetSource::Remote {
                token: crate::shared::REMOTE_RESET_TOKEN_MAGIC,
                seq: 1
            })
        );
    }

    #[test]
    fn mode_request_decodes() {
        let frame = RawFrame::from_slice(Bus::Low, 0x111, &[0x01, 0x2a]).unwrap();
        assert_eq!(decode_mode_request(&frame), Some(true));
        let other = RawFrame::from_slice(Bus::Low, 0x112, &[0x01, 0x07]).unwrap();
        assert_eq!(decode_mode_request(&other), None);
    }

    #[test]
    fn estop_presence() {
        let frame = encode_estop();
        assert_eq!(frame.dlc, 0);
        assert!(is_estop(&frame));
        assert!(!is_estop(
            &RawFrame::from_slice(Bus::Low, 0x110, &[0, 0]).unwrap()
        ));
    }

    #[test]
    fn reset_response_reports_rejection() {
        let mut blockers = ResetBlockers::empty();
        blockers.add(crate::shared::ResetBlocker::Moving);
        let frame = encode_reset_rsp(3, false, blockers, 9).unwrap();
        let decoded = generated::SysEstopResetRsp::unpack(frame.payload()).unwrap();
        assert!(decoded.result, "rejected must encode result=1");
        assert_eq!(decoded.blocker_mask, blockers.bits() as u16);
    }
}
