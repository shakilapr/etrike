//! Body light logic (indicators, headlight, brake lamp).

use crate::shared::Mode;

/// Host light command bits (from `0x302`).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LightBits {
    pub left: bool,
    pub right: bool,
    pub brake: bool,
    pub head: bool,
}

/// Physical switch inputs.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LightSwitches {
    pub left: bool,
    pub right: bool,
    pub head: bool,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LightInputs {
    pub switches: LightSwitches,
    pub lever: bool,
    pub estop: bool,
    pub host: Option<LightBits>,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LightOutputs {
    pub left: bool,
    pub right: bool,
    pub head: bool,
    pub brake: bool,
}

/// Evaluate the light outputs. Indicators and headlight follow the host command
/// in AUTO (when present) or the handlebar switches otherwise. The brake lamp is
/// an OR of the lever, ESTOP, and host brake bit.
pub fn evaluate(mode: Mode, inputs: LightInputs) -> LightOutputs {
    let (left, right, head) = match (mode, inputs.host) {
        (Mode::Auto, Some(bits)) => (bits.left, bits.right, bits.head),
        _ => (
            inputs.switches.left,
            inputs.switches.right,
            inputs.switches.head,
        ),
    };
    let brake = inputs.lever
        || inputs.estop
        || matches!(mode, Mode::Auto) && inputs.host.map(|bits| bits.brake).unwrap_or(false);
    LightOutputs {
        left,
        right,
        head,
        brake,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn switches(left: bool, right: bool, head: bool) -> LightSwitches {
        LightSwitches { left, right, head }
    }

    #[test]
    fn manual_uses_switches() {
        let outputs = evaluate(
            Mode::Manual,
            LightInputs {
                switches: switches(true, false, true),
                lever: false,
                estop: false,
                host: Some(LightBits {
                    left: false,
                    right: true,
                    brake: true,
                    head: false,
                }),
            },
        );
        assert!(outputs.left);
        assert!(!outputs.right);
        assert!(outputs.head);
        // Host brake bit is ignored in MANUAL.
        assert!(!outputs.brake);
    }

    #[test]
    fn auto_uses_host_bits_and_brake_or() {
        let outputs = evaluate(
            Mode::Auto,
            LightInputs {
                switches: switches(true, true, true),
                lever: true,
                estop: false,
                host: Some(LightBits {
                    left: false,
                    right: true,
                    brake: false,
                    head: false,
                }),
            },
        );
        assert!(!outputs.left);
        assert!(outputs.right);
        assert!(!outputs.head);
        assert!(outputs.brake);
    }

    #[test]
    fn estop_lights_brake() {
        let outputs = evaluate(
            Mode::Manual,
            LightInputs {
                switches: switches(false, false, false),
                lever: false,
                estop: true,
                host: None,
            },
        );
        assert!(outputs.brake);
    }
}
