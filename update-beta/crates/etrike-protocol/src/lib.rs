//! E-Trike CAN wire-contract crate.
//!
//! Owns the canonical wire contract and the validation primitives every data
//! pipeline passes through. Generated payload codecs live in
//! `protocol/generated/rust/etrike_protocol.rs`; this crate wraps them.
//!
//! See `architecture.md` in this crate for the design.
#![cfg_attr(not(test), no_std)]

pub mod frame;
pub mod validity;

pub use frame::{Bus, CodecStatus, RawFrame};
pub use validity::{fresh, StreamStatus, StreamValidity, Timestamped, Valid};
