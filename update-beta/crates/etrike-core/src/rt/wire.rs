//! RT CAN I/O mapping for the authority inputs and drive output.

use etrike_protocol::generated;
use etrike_protocol::{Bus, CodecStatus, RawFrame};

/// Decode SYS mode command (`0x110`); returns the AUTO flag.
pub fn decode_mode_auto(frame: &RawFrame) -> Option<bool> {
    if frame.id != generated::SysModeCmd::ID {
        return None;
    }
    generated::SysModeCmd::unpack(frame.payload())
        .ok()
        .map(|msg| msg.mode)
}

/// Decode SYS safety status (`0x011`); returns whether ESTOP is active.
pub fn decode_safety_estop(frame: &RawFrame) -> Option<bool> {
    if frame.id != generated::SysSafetySts::ID {
        return None;
    }
    generated::SysSafetySts::unpack(frame.payload())
        .ok()
        .map(|msg| msg.estop_source != 0)
}

/// Encode RT drive command (`0x204`).
pub fn encode_drive_cmd(speed_mmps: i32, gear: u8) -> Result<RawFrame, CodecStatus> {
    let message = generated::RtDriveCmd {
        motor_speed_mmps: speed_mmps,
        gear,
    };
    let mut frame = RawFrame::new(Bus::Low, generated::RtDriveCmd::ID);
    frame.dlc = generated::RtDriveCmd::DLC as u8;
    let status = message.pack(&mut frame.data[..generated::RtDriveCmd::DLC]);
    if status != CodecStatus::Ok {
        return Err(status);
    }
    Ok(frame)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mode_decodes() {
        let frame = RawFrame::from_slice(Bus::Low, 0x110, &[0x01, 0x07]).unwrap();
        assert_eq!(decode_mode_auto(&frame), Some(true));
        let manual = RawFrame::from_slice(Bus::Low, 0x110, &[0x00, 0x01]).unwrap();
        assert_eq!(decode_mode_auto(&manual), Some(false));
    }

    #[test]
    fn safety_estop_decodes() {
        let estop = RawFrame::from_slice(Bus::Low, 0x011, &[0x02, 0x00, 0x00, 0x00, 0x00]).unwrap();
        assert_eq!(decode_safety_estop(&estop), Some(true));
        let clear = RawFrame::from_slice(Bus::Low, 0x011, &[0x00, 0x00, 0x00, 0x00, 0x00]).unwrap();
        assert_eq!(decode_safety_estop(&clear), Some(false));
    }

    #[test]
    fn drive_cmd_matches_canonical_vector() {
        // rt-drive-negative from protocol/vectors/payload-v1.json.
        let frame = encode_drive_cmd(-500, 3).unwrap();
        assert_eq!(frame.id, 0x204);
        assert_eq!(frame.payload(), &[0xff, 0xff, 0xfe, 0x0c, 0x03]);
    }
}
