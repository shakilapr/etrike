//! Shared authority value types.

/// Vehicle mode. `Estop` is reported as `Manual` on `0x110` (mode is not the
/// ESTOP channel); this enum is the internal resolved state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Manual,
    Auto,
    Estop,
}

/// Resolved power command (`0x113`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Power {
    Off,
    On,
}

impl Power {
    pub const fn on() -> Self {
        Power::On
    }

    pub const fn off() -> Self {
        Power::Off
    }
}

/// Reason motion authority is clamped, if any.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Clamp {
    None,
    Estop,
}

/// Result of `resolve_authority`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Authority {
    pub mode: Mode,
    pub power: Power,
    pub clamp: Clamp,
}
