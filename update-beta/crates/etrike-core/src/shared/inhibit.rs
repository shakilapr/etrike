//! Two-mask fault model: transient inhibits (self-clearing) and latched faults
//! (cleared only through the validated reset transaction).

/// A transient, self-clearing inhibit.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InhibitReason {
    MtrFbkLoss,
    SebCommsLoss,
    BrakeFollowing,
}

impl InhibitReason {
    const fn bit(self) -> u8 {
        match self {
            InhibitReason::MtrFbkLoss => 1 << 0,
            InhibitReason::SebCommsLoss => 1 << 1,
            InhibitReason::BrakeFollowing => 1 << 2,
        }
    }
}

/// A fault that latches until an authenticated reset.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LatchedFault {
    BrakeFollowing,
    SebL3,
}

impl LatchedFault {
    const fn bit(self) -> u8 {
        match self {
            LatchedFault::BrakeFollowing => 1 << 0,
            LatchedFault::SebL3 => 1 << 1,
        }
    }
}

/// Holds the transient and latched masks.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct InhibitModel {
    transient: u8,
    latched: u8,
}

impl InhibitModel {
    pub const fn new() -> Self {
        Self {
            transient: 0,
            latched: 0,
        }
    }

    pub fn set_transient(&mut self, reason: InhibitReason) {
        self.transient |= reason.bit();
    }

    pub fn clear_transient(&mut self, reason: InhibitReason) {
        self.transient &= !reason.bit();
    }

    pub fn transient_contains(&self, reason: InhibitReason) -> bool {
        self.transient & reason.bit() != 0
    }

    pub fn transient_any(&self) -> bool {
        self.transient != 0
    }

    pub fn latch(&mut self, fault: LatchedFault) {
        self.latched |= fault.bit();
    }

    pub fn latched_contains(&self, fault: LatchedFault) -> bool {
        self.latched & fault.bit() != 0
    }

    pub fn latched_any(&self) -> bool {
        self.latched != 0
    }

    /// Clear all latched faults. Only the validated reset transaction may call
    /// this.
    pub fn clear_latched(&mut self) {
        self.latched = 0;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transient_self_clears() {
        let mut model = InhibitModel::new();
        model.set_transient(InhibitReason::MtrFbkLoss);
        assert!(model.transient_any());
        model.clear_transient(InhibitReason::MtrFbkLoss);
        assert!(!model.transient_any());
    }

    #[test]
    fn latched_persists_until_explicit_clear() {
        let mut model = InhibitModel::new();
        model.latch(LatchedFault::SebL3);
        model.set_transient(InhibitReason::SebCommsLoss);
        model.clear_transient(InhibitReason::SebCommsLoss);
        assert!(model.latched_any());
        assert!(model.latched_contains(LatchedFault::SebL3));
        model.clear_latched();
        assert!(!model.latched_any());
    }
}
