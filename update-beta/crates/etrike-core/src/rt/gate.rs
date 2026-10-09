//! Motion-authority gate with the asymmetric ESTOP clear.
//!
//! Authority is lost immediately when any stream is invalid or ESTOP is active,
//! but only regained after two consecutive clean frames (mirrors the C++
//! `0x011` two-frame clear). RT produces no motion output without authority.

/// Tracks RT drive authority.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct AuthorityGate {
    clean_frames: u8,
    have: bool,
}

/// Clean frames required before authority is regained.
pub const CLEAN_FRAMES_REQUIRED: u8 = 2;

impl AuthorityGate {
    pub const fn new() -> Self {
        Self {
            clean_frames: 0,
            have: false,
        }
    }

    /// Observe one SYS status tick. `safety_valid` and `mode_valid` are the
    /// stream-validity results; `estop_active` comes from `0x011`.
    pub fn observe(&mut self, safety_valid: bool, mode_valid: bool, estop_active: bool) -> bool {
        if !safety_valid || !mode_valid || estop_active {
            self.have = false;
            self.clean_frames = 0;
            return false;
        }
        self.clean_frames = self.clean_frames.saturating_add(1);
        if self.clean_frames >= CLEAN_FRAMES_REQUIRED {
            self.have = true;
        }
        self.have
    }

    pub const fn allows(&self) -> bool {
        self.have
    }

    pub fn reset(&mut self) {
        self.clean_frames = 0;
        self.have = false;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn starts_without_authority() {
        assert!(!AuthorityGate::new().allows());
    }

    #[test]
    fn requires_two_clean_frames() {
        let mut gate = AuthorityGate::new();
        assert!(!gate.observe(true, true, false));
        assert!(!gate.allows());
        assert!(gate.observe(true, true, false));
        assert!(gate.allows());
    }

    #[test]
    fn estop_drops_authority_immediately() {
        let mut gate = AuthorityGate::new();
        gate.observe(true, true, false);
        gate.observe(true, true, false);
        assert!(gate.allows());
        // One ESTOP frame removes authority at once.
        assert!(!gate.observe(true, true, true));
        assert!(!gate.allows());
        // And it takes two clean frames to come back.
        assert!(!gate.observe(true, true, false));
        assert!(!gate.allows());
        assert!(gate.observe(true, true, false));
        assert!(gate.allows());
    }

    #[test]
    fn invalid_stream_withdraws_authority() {
        let mut gate = AuthorityGate::new();
        gate.observe(true, true, false);
        gate.observe(true, true, false);
        assert!(gate.allows());
        assert!(!gate.observe(false, true, false));
        assert!(!gate.allows());
    }
}
