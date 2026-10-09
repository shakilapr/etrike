//! Node-presence bit vocabulary, shared between SYS `0x011`/`0x500` and RT
//! `0x501` (see `shared/estop_status.h`).

use etrike_protocol::validity::{fresh, Instant, Timestamped};

pub const NODE_RT: u8 = 1 << 0;
pub const NODE_MTR: u8 = 1 << 1;
pub const NODE_SEB: u8 = 1 << 2;
pub const NODE_SES: u8 = 1 << 3;
pub const NODE_HOST: u8 = 1 << 4;
pub const NODE_SYS: u8 = 1 << 5;

/// Freshness window for a node-presence bit (ms).
pub const NODE_PRESENCE_FRESH_MS: u64 = 500;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct NodePresence(u8);

impl NodePresence {
    pub const fn new() -> Self {
        Self(0)
    }

    pub const fn bits(&self) -> u8 {
        self.0
    }

    pub fn set(&mut self, bit: u8) {
        self.0 |= bit;
    }

    pub fn clear(&mut self, bit: u8) {
        self.0 &= !bit;
    }

    pub fn contains(&self, bit: u8) -> bool {
        self.0 & bit != 0
    }

    /// True when `seen` is present and its timestamp is within the window.
    pub fn is_fresh(seen: Option<Timestamped<()>>, now: Instant) -> bool {
        seen.map(|ts| fresh(&ts, now, NODE_PRESENCE_FRESH_MS))
            .unwrap_or(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn set_and_clear_bits() {
        let mut presence = NodePresence::new();
        presence.set(NODE_RT);
        presence.set(NODE_SYS);
        assert!(presence.contains(NODE_RT));
        assert!(presence.contains(NODE_SYS));
        assert!(!presence.contains(NODE_MTR));
        presence.clear(NODE_RT);
        assert!(!presence.contains(NODE_RT));
    }

    #[test]
    fn freshness_window() {
        assert!(NodePresence::is_fresh(Some(Timestamped::new((), 100)), 300));
        assert!(!NodePresence::is_fresh(
            Some(Timestamped::new((), 100)),
            900
        ));
        assert!(!NodePresence::is_fresh(None, 0));
    }
}
