//! E-Trike pure domain logic.
//!
//! Deterministic, hardware-free state machines for SYS and RT. No HAL, no I/O,
//! no globals. See `architecture.md` in this crate.
#![cfg_attr(not(test), no_std)]

pub mod rt;
pub mod shared;
pub mod sys;
