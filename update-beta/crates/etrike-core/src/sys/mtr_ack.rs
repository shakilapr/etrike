//! MTR ESTOP acknowledgement watchdog.
//!
//! Arms on ESTOP and expects the MTR to acknowledge. If acknowledgement does
//! not arrive within the timeout, it retries and then latches an exhausted
//! fault. The C++ version declared this watchdog but never advanced it, so the
//! exhaustion path was unreachable; here `tick` is the only driver and the
//! path is exercised by tests.

/// Current watchdog state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AckState {
    Idle,
    Awaiting,
    Confirmed,
    Exhausted,
}

/// MTR ESTOP acknowledgement watchdog.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MtrAckWatchdog {
    timeout_ms: u64,
    max_retries: u32,
    state: AckState,
    deadline: u64,
    retries: u32,
}

impl MtrAckWatchdog {
    pub const fn new(timeout_ms: u64, max_retries: u32) -> Self {
        Self {
            timeout_ms,
            max_retries,
            state: AckState::Idle,
            deadline: 0,
            retries: 0,
        }
    }

    /// Arm the watchdog at time `now` (called on ESTOP entry).
    pub fn arm(&mut self, now: u64) {
        self.state = AckState::Awaiting;
        self.deadline = now.saturating_add(self.timeout_ms);
        self.retries = 0;
    }

    /// Record an acknowledgement.
    pub fn on_ack(&mut self) {
        if matches!(self.state, AckState::Awaiting) {
            self.state = AckState::Confirmed;
        }
    }

    /// Disarm (reset complete).
    pub fn reset(&mut self) {
        self.state = AckState::Idle;
        self.retries = 0;
    }

    /// Advance the watchdog. Returns the resulting state.
    pub fn tick(&mut self, now: u64) -> AckState {
        if matches!(self.state, AckState::Awaiting) && now >= self.deadline {
            if self.retries >= self.max_retries {
                self.state = AckState::Exhausted;
            } else {
                self.retries += 1;
                self.deadline = now.saturating_add(self.timeout_ms);
            }
        }
        self.state
    }

    pub const fn state(&self) -> AckState {
        self.state
    }

    pub const fn is_exhausted(&self) -> bool {
        matches!(self.state, AckState::Exhausted)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn confirms_on_ack() {
        let mut watchdog = MtrAckWatchdog::new(100, 3);
        watchdog.arm(1000);
        assert_eq!(watchdog.state(), AckState::Awaiting);
        watchdog.on_ack();
        assert_eq!(watchdog.state(), AckState::Confirmed);
        assert_eq!(watchdog.tick(9999), AckState::Confirmed);
    }

    #[test]
    fn mtr_ack_watchdog_exhausts_and_latches() {
        let mut watchdog = MtrAckWatchdog::new(100, 3);
        watchdog.arm(0);
        // Four timeouts (initial + 3 retries) exhaust it.
        assert_eq!(watchdog.tick(100), AckState::Awaiting);
        assert_eq!(watchdog.tick(200), AckState::Awaiting);
        assert_eq!(watchdog.tick(300), AckState::Awaiting);
        assert_eq!(watchdog.tick(400), AckState::Exhausted);
        assert!(watchdog.is_exhausted());
    }

    #[test]
    fn reset_disarms() {
        let mut watchdog = MtrAckWatchdog::new(100, 1);
        watchdog.arm(0);
        watchdog.tick(100);
        watchdog.reset();
        assert_eq!(watchdog.state(), AckState::Idle);
    }
}
