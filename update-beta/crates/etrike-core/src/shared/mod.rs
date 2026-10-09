//! Semantics shared by the SYS and RT cores.

pub mod authority;
pub mod estop;
pub mod inhibit;
pub mod presence;

pub use authority::{Authority, Clamp, Mode, Power};
pub use estop::{
    EstopLatch, EstopReason, ResetBlocker, ResetBlockers, ResetEvidence, ResetOutcome, ResetSource,
    REMOTE_RESET_TOKEN_MAGIC,
};
pub use inhibit::{InhibitModel, InhibitReason, LatchedFault};
pub use presence::NodePresence;
