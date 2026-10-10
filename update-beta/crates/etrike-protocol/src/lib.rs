//! E-Trike CAN wire-contract crate.
//!
//! Owns the canonical wire contract and the validation primitives every data
//! pipeline passes through. The typed payload codecs are generated from
//! `protocol/contracts/*.yaml` into
//! `protocol/generated/rust/etrike_protocol.rs` and included below.
//!
//! See `architecture.md` in this crate for the design.
#![cfg_attr(not(test), no_std)]

pub mod e2e;
pub mod frame;
pub mod validity;

pub use frame::{Bus, CodecStatus, RawFrame};
pub use validity::{fresh, StreamStatus, StreamValidity, Timestamped, Valid};

/// Generated wire codecs and routing metadata.
///
/// The module is `include!`d from the canonical generated output so the wire
/// contract keeps a single home.
#[allow(dead_code, unused_mut, unused_variables, unused_parens, clippy::all)]
pub mod generated {
    use crate::CodecStatus;
    include!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../../protocol/generated/rust/etrike_protocol.rs"
    ));
}

/// Map a generated bus name to the crate [`Bus`] enum.
pub fn bus_from_name(name: &str) -> Option<Bus> {
    match name {
        "high" => Some(Bus::High),
        "low" => Some(Bus::Low),
        "powertrain" => Some(Bus::Powertrain),
        _ => None,
    }
}

/// The generated name of a [`Bus`].
pub const fn bus_name(bus: Bus) -> &'static str {
    match bus {
        Bus::High => "high",
        Bus::Low => "low",
        Bus::Powertrain => "powertrain",
    }
}

/// Verify that `(bus, id)` has an owner and return the declared sender.
pub fn tx_owner(bus: Bus, id: u32) -> Option<&'static str> {
    generated::TX_OWNERS
        .iter()
        .find(|owner| owner.bus == bus_name(bus) && owner.id == id)
        .map(|owner| owner.sender)
}

/// Verify a raw frame against the wire contract before it may be used.
///
/// Checks that the `(bus, id)` is a known message, that the DLC matches the
/// contract, and (for E2E-protected messages) that the CRC is correct. A frame
/// that fails `verify` must be discarded; only frames that pass may be decoded
/// and passed to the core.
pub fn verify(frame: &RawFrame) -> CodecStatus {
    let Some(meta) = generated::lookup(bus_name(frame.bus), frame.id) else {
        return CodecStatus::WrongMessageId;
    };
    if frame.dlc != meta.dlc {
        return CodecStatus::UnexpectedLength;
    }
    match frame.id {
        generated::SysSafetySts::ID => {
            if e2e::sys_safety_sts_crc(frame.payload()) != frame.data[4] {
                return CodecStatus::ChecksumMismatch;
            }
        }
        generated::SysNodeStatus::ID
        | generated::RtNodeStatus::ID
        | generated::MtrNodeStatus::ID => {
            if e2e::node_status_crc(frame.payload()) != frame.data[7] {
                return CodecStatus::ChecksumMismatch;
            }
        }
        _ => {}
    }
    CodecStatus::Ok
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bus_name_round_trips() {
        for bus in [Bus::High, Bus::Low, Bus::Powertrain] {
            let name = match bus {
                Bus::High => "high",
                Bus::Low => "low",
                Bus::Powertrain => "powertrain",
            };
            assert_eq!(bus_from_name(name), Some(bus));
        }
        assert_eq!(bus_from_name("nope"), None);
    }

    #[test]
    fn tx_owner_lookup_works() {
        // SYS is the sole owner of 0x7B9 on the low bus.
        assert_eq!(tx_owner(Bus::Low, 0x7B9), Some("SYS"));
        // 0x001 is multi-sender (Any).
        assert_eq!(tx_owner(Bus::Low, 0x001), Some("Any"));
    }

    #[test]
    fn decode_matches_canonical_vectors() {
        // Bytes taken from protocol/vectors/payload-v1.json (the C++ golden set).
        let hmi = generated::HmiModeReq::unpack(&[0x01, 0x2a]).unwrap();
        assert!(hmi.req_mode);
        assert_eq!(hmi.rolling_counter, 42);
        let mut out = [0u8; 2];
        assert_eq!(hmi.pack(&mut out), CodecStatus::Ok);
        assert_eq!(out, [0x01, 0x2a]);

        let mtr = generated::MtrMotorFbk::unpack(&[0xff, 0xfe, 0x03, 0x11]).unwrap();
        assert_eq!(mtr.motor_command_speed_mmps, -2);
        assert_eq!(mtr.gear_state, 3);
        assert_eq!(mtr.fault_flags, 17);

        // 32-bit signed speed = 1000, 24-bit signed yaw = -2, gear = 3.
        let drive =
            generated::HostDriveCmd::unpack(&[0x00, 0x00, 0x03, 0xe8, 0xff, 0xff, 0xfe, 0x03])
                .unwrap();
        assert_eq!(drive.speed_mmps, 1000);
        assert_eq!(drive.yaw_rate_mrad_s, -2);
        assert_eq!(drive.gear, 3);
        let mut bytes = [0u8; 8];
        assert_eq!(drive.pack(&mut bytes), CodecStatus::Ok);
        assert_eq!(bytes, [0x00, 0x00, 0x03, 0xe8, 0xff, 0xff, 0xfe, 0x03]);
    }

    #[test]
    fn wrong_length_is_rejected() {
        assert_eq!(
            generated::HostDriveCmd::unpack(&[0x00, 0x00, 0x03, 0xe8]),
            Err(CodecStatus::UnexpectedLength)
        );
    }

    #[test]
    fn verify_accepts_known_and_rejects_unknown() {
        let ok = RawFrame::from_slice(Bus::Low, 0x110, &[0x01, 0x07]).unwrap();
        assert_eq!(verify(&ok), CodecStatus::Ok);
        let short = RawFrame::from_slice(Bus::Low, 0x110, &[0x01]).unwrap();
        assert_eq!(verify(&short), CodecStatus::UnexpectedLength);
        let unknown = RawFrame::from_slice(Bus::Low, 0x7FF, &[]).unwrap();
        assert_eq!(verify(&unknown), CodecStatus::WrongMessageId);
        // 0x011 with a correct E2E CRC passes; a wrong CRC is rejected.
        let good = RawFrame::from_slice(Bus::Low, 0x011, &[0, 0, 0, 0, 0x30]).unwrap();
        assert_eq!(verify(&good), CodecStatus::Ok);
        let bad = RawFrame::from_slice(Bus::Low, 0x011, &[0, 0, 0, 0, 0x00]).unwrap();
        assert_eq!(verify(&bad), CodecStatus::ChecksumMismatch);
    }
}
