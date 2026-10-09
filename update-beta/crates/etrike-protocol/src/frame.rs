//! CAN frame representation and codec status vocabulary.

/// Logical CAN bus.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
pub enum Bus {
    High,
    Low,
    Powertrain,
}

/// Portable codec status vocabulary, mirroring `protocol/generated/errors.json`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[cfg_attr(feature = "defmt", derive(defmt::Format))]
pub enum CodecStatus {
    Ok,
    WrongMessageId,
    WrongFrameFormat,
    UnexpectedLength,
    ValueOutOfRange,
    InvalidEnum,
    ConstantMismatch,
    ChecksumMismatch,
    UnsupportedSemantics,
    NullData,
}

impl CodecStatus {
    /// True when the operation succeeded.
    pub const fn succeeded(self) -> bool {
        matches!(self, CodecStatus::Ok)
    }
}

/// A classic CAN (2.0A/B) frame with at most 8 significant payload bytes.
///
/// This is an inert value: it carries no behaviour that can affect the
/// vehicle. Frames must be validated before use.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RawFrame {
    pub bus: Bus,
    pub id: u32,
    pub dlc: u8,
    pub data: [u8; 8],
}

impl RawFrame {
    /// Construct a zeroed frame on `bus` with the given id.
    pub const fn new(bus: Bus, id: u32) -> Self {
        Self {
            bus,
            id,
            dlc: 0,
            data: [0; 8],
        }
    }

    /// Construct a frame from a payload slice. `dlc` is the slice length and
    /// must not exceed 8.
    pub fn from_slice(bus: Bus, id: u32, payload: &[u8]) -> Option<Self> {
        if payload.len() > 8 {
            return None;
        }
        let mut data = [0u8; 8];
        data[..payload.len()].copy_from_slice(payload);
        Some(Self {
            bus,
            id,
            dlc: payload.len() as u8,
            data,
        })
    }

    /// The significant payload bytes (`dlc` bytes).
    pub fn payload(&self) -> &[u8] {
        &self.data[..self.dlc.min(8) as usize]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn from_slice_rejects_oversized() {
        assert!(RawFrame::from_slice(Bus::Low, 0x1, &[0u8; 9]).is_none());
    }

    #[test]
    fn from_slice_truncates_to_dlc() {
        let f = RawFrame::from_slice(Bus::High, 0x300, &[1, 2, 3]).unwrap();
        assert_eq!(f.dlc, 3);
        assert_eq!(f.payload(), &[1, 2, 3]);
    }

    #[test]
    fn succeeded_matches_ok() {
        assert!(CodecStatus::Ok.succeeded());
        assert!(!CodecStatus::ChecksumMismatch.succeeded());
    }
}
