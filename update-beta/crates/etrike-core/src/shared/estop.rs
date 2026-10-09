//! ESTOP latch and the validated reset transaction.
//!
//! The latch can only be entered via [`EstopLatch::enter`] and left via
//! [`EstopLatch::try_reset`]. There is no API that clears it without evidence,
//! so "reset while blocked" cannot be expressed.

/// Why the vehicle entered ESTOP.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EstopReason {
    HwButton,
    RtHbLost,
    Can001,
    MtrFault,
    EgasFault,
    TaskDeadline,
    CanBusoff,
}

/// A request to leave ESTOP.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResetSource {
    /// START control transition (policy-defined edge).
    StartLatch,
    /// MODE button held past the long-press threshold.
    ModeLongPress,
    /// Authenticated remote reset (host `0x114`).
    Remote { token: u16, seq: u8 },
}

/// Magic token that a remote reset must present.
pub const REMOTE_RESET_TOKEN_MAGIC: u16 = 0x5253;

impl ResetSource {
    /// A remote reset is only valid when it carries the magic token.
    pub fn is_valid(self) -> bool {
        match self {
            ResetSource::Remote { token, .. } => token == REMOTE_RESET_TOKEN_MAGIC,
            ResetSource::StartLatch | ResetSource::ModeLongPress => true,
        }
    }
}

/// A condition that forbids leaving ESTOP.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResetBlocker {
    PhysicalEstop,
    LatchedFault,
    Moving,
    HeartbeatLoss,
    MtrEstopActive,
    TransientInhibit,
    InvalidToken,
}

impl ResetBlocker {
    const fn bit(self) -> u8 {
        match self {
            ResetBlocker::PhysicalEstop => 1 << 0,
            ResetBlocker::LatchedFault => 1 << 1,
            ResetBlocker::Moving => 1 << 2,
            ResetBlocker::HeartbeatLoss => 1 << 3,
            ResetBlocker::MtrEstopActive => 1 << 4,
            ResetBlocker::TransientInhibit => 1 << 5,
            ResetBlocker::InvalidToken => 1 << 6,
        }
    }

    const ALL: [ResetBlocker; 7] = [
        ResetBlocker::PhysicalEstop,
        ResetBlocker::LatchedFault,
        ResetBlocker::Moving,
        ResetBlocker::HeartbeatLoss,
        ResetBlocker::MtrEstopActive,
        ResetBlocker::TransientInhibit,
        ResetBlocker::InvalidToken,
    ];
}

/// A set of active [`ResetBlocker`]s.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ResetBlockers(u8);

impl ResetBlockers {
    pub const fn empty() -> Self {
        Self(0)
    }

    pub fn add(&mut self, blocker: ResetBlocker) {
        self.0 |= blocker.bit();
    }

    pub fn contains(&self, blocker: ResetBlocker) -> bool {
        self.0 & blocker.bit() != 0
    }

    pub fn any(&self) -> bool {
        self.0 != 0
    }

    /// The first blocker in priority order, if any.
    pub fn first(&self) -> Option<ResetBlocker> {
        ResetBlocker::ALL
            .into_iter()
            .find(|blocker| self.contains(*blocker))
    }

    pub const fn bits(&self) -> u8 {
        self.0
    }
}

/// Evidence presented to [`EstopLatch::try_reset`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ResetEvidence {
    pub source: ResetSource,
    pub blockers: ResetBlockers,
}

/// Outcome of a reset attempt.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ResetOutcome {
    Cleared { epoch: u32 },
    Rejected(ResetBlocker),
}

/// Latched emergency-stop state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EstopLatch {
    active: bool,
    reason: Option<EstopReason>,
    epoch: u32,
}

impl Default for EstopLatch {
    fn default() -> Self {
        Self::new()
    }
}

impl EstopLatch {
    pub const fn new() -> Self {
        Self {
            active: false,
            reason: None,
            epoch: 0,
        }
    }

    /// Enter ESTOP. Idempotent; the first reason wins until reset.
    pub fn enter(&mut self, reason: EstopReason) {
        if !self.active {
            self.reason = Some(reason);
        }
        self.active = true;
        self.epoch = self.epoch.wrapping_add(1);
    }

    pub const fn is_active(&self) -> bool {
        self.active
    }

    pub const fn reason(&self) -> Option<EstopReason> {
        self.reason
    }

    pub const fn epoch(&self) -> u32 {
        self.epoch
    }

    /// Attempt to leave ESTOP. Fails while any blocker is set or the source is
    /// not valid. This is the only way to clear the latch.
    pub fn try_reset(&mut self, evidence: ResetEvidence) -> ResetOutcome {
        if let Some(blocker) = evidence.blockers.first() {
            return ResetOutcome::Rejected(blocker);
        }
        if !evidence.source.is_valid() {
            return ResetOutcome::Rejected(ResetBlocker::InvalidToken);
        }
        self.active = false;
        self.reason = None;
        self.epoch = self.epoch.wrapping_add(1);
        ResetOutcome::Cleared { epoch: self.epoch }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn starts_inactive() {
        let latch = EstopLatch::new();
        assert!(!latch.is_active());
        assert_eq!(latch.reason(), None);
    }

    #[test]
    fn enter_latches_and_keeps_first_reason() {
        let mut latch = EstopLatch::new();
        latch.enter(EstopReason::HwButton);
        latch.enter(EstopReason::Can001);
        assert!(latch.is_active());
        assert_eq!(latch.reason(), Some(EstopReason::HwButton));
    }

    /// Exhaustive: for every non-empty blocker set, reset must be rejected and
    /// the latch must stay active.
    #[test]
    fn estop_never_resets_with_blocker() {
        let mut round = 0u32;
        for mask in 1u8..(1u8 << 7) {
            let mut blockers = ResetBlockers::empty();
            for blocker in ResetBlocker::ALL {
                if mask & blocker.bit() != 0 {
                    blockers.add(blocker);
                }
            }
            for source in [
                ResetSource::StartLatch,
                ResetSource::ModeLongPress,
                ResetSource::Remote {
                    token: REMOTE_RESET_TOKEN_MAGIC,
                    seq: 1,
                },
            ] {
                round += 1;
                let _ = round;
                let mut latch = EstopLatch::new();
                latch.enter(EstopReason::HwButton);
                let outcome = latch.try_reset(ResetEvidence { source, blockers });
                assert!(matches!(outcome, ResetOutcome::Rejected(_)));
                assert!(latch.is_active(), "latch cleared with blockers {mask:#x}");
            }
        }
    }

    #[test]
    fn resets_only_with_valid_evidence() {
        let mut latch = EstopLatch::new();
        latch.enter(EstopReason::Can001);
        let outcome = latch.try_reset(ResetEvidence {
            source: ResetSource::ModeLongPress,
            blockers: ResetBlockers::empty(),
        });
        assert!(matches!(outcome, ResetOutcome::Cleared { .. }));
        assert!(!latch.is_active());
        assert_eq!(latch.reason(), None);
    }

    #[test]
    fn remote_reset_requires_magic_token() {
        let mut latch = EstopLatch::new();
        latch.enter(EstopReason::Can001);
        let outcome = latch.try_reset(ResetEvidence {
            source: ResetSource::Remote {
                token: 0x1234,
                seq: 9,
            },
            blockers: ResetBlockers::empty(),
        });
        assert_eq!(outcome, ResetOutcome::Rejected(ResetBlocker::InvalidToken));
        assert!(latch.is_active());
    }

    #[test]
    fn blocker_priority_order() {
        let mut blockers = ResetBlockers::empty();
        blockers.add(ResetBlocker::TransientInhibit);
        blockers.add(ResetBlocker::PhysicalEstop);
        assert_eq!(blockers.first(), Some(ResetBlocker::PhysicalEstop));
    }
}
