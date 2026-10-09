//! Brake arbitration — exactly one winner, in a fixed priority order.
//!
//! Priority (highest first): ESTOP max stroke, physical lever, RT `0x205`
//! pressure intent, released. Because a single function returns the winner,
//! two sources cannot each write the brake command.

/// Which source produced the brake command.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BrakeSource {
    Estop,
    Lever,
    RtIntent,
    Released,
}

/// Brake command in 0.1 mm stroke units (matches the SEB stroke scale).
pub const STROKE_ESTOP: u16 = 270;
pub const STROKE_LEVER: u16 = 150;
pub const STROKE_RELEASED: u16 = 0;
/// Maximum knee-point pressure in kPa accepted from RT intent.
pub const MAX_INTENT_KPA: u16 = 5000;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BrakeCommand {
    pub source: BrakeSource,
    pub stroke: u16,
}

/// Arbitrate the brake command.
pub fn arbitrate(estop: bool, lever: bool, rt_intent_kpa: Option<u16>) -> BrakeCommand {
    if estop {
        return BrakeCommand {
            source: BrakeSource::Estop,
            stroke: STROKE_ESTOP,
        };
    }
    if lever {
        return BrakeCommand {
            source: BrakeSource::Lever,
            stroke: STROKE_LEVER,
        };
    }
    if let Some(kpa) = rt_intent_kpa {
        return BrakeCommand {
            source: BrakeSource::RtIntent,
            // Map the clamped kPa request onto the stroke scale.
            stroke: (kpa.min(MAX_INTENT_KPA) as u32 * STROKE_ESTOP as u32 / MAX_INTENT_KPA as u32)
                as u16,
        };
    }
    BrakeCommand {
        source: BrakeSource::Released,
        stroke: STROKE_RELEASED,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn estop_dominates_everything() {
        let command = arbitrate(true, true, Some(MAX_INTENT_KPA));
        assert_eq!(command.source, BrakeSource::Estop);
        assert_eq!(command.stroke, STROKE_ESTOP);
    }

    #[test]
    fn lever_dominates_rt_intent() {
        let command = arbitrate(false, true, Some(1000));
        assert_eq!(command.source, BrakeSource::Lever);
        assert_eq!(command.stroke, STROKE_LEVER);
    }

    #[test]
    fn rt_intent_when_alone() {
        let command = arbitrate(false, false, Some(MAX_INTENT_KPA));
        assert_eq!(command.source, BrakeSource::RtIntent);
        assert_eq!(command.stroke, STROKE_ESTOP);
    }

    #[test]
    fn released_when_nothing() {
        let command = arbitrate(false, false, None);
        assert_eq!(command.source, BrakeSource::Released);
        assert_eq!(command.stroke, STROKE_RELEASED);
    }

    /// Exhaustive priority check.
    #[test]
    fn priority_is_total() {
        for estop in [false, true] {
            for lever in [false, true] {
                for intent in [None, Some(0u16), Some(2500), Some(5000), Some(9000)] {
                    let command = arbitrate(estop, lever, intent);
                    let expected = if estop {
                        BrakeSource::Estop
                    } else if lever {
                        BrakeSource::Lever
                    } else if intent.is_some() {
                        BrakeSource::RtIntent
                    } else {
                        BrakeSource::Released
                    };
                    assert_eq!(command.source, expected);
                    // Monotonicity: ESTOP >= lever >= any intent >= released.
                    assert!(command.stroke <= STROKE_ESTOP);
                }
            }
        }
    }
}
