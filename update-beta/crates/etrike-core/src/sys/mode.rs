//! Mode and power resolution — the single place mode is decided.
//!
//! Host requests are subordinate inputs. Physical controls and the ESTOP latch
//! take precedence, so a host AUTO request can never override a physical/SYS
//! MANUAL decision.

use crate::shared::{Authority, Clamp, Mode, Power};

/// Inputs to mode resolution.
#[derive(Debug, Clone, Copy)]
pub struct ModeInputs {
    /// ESTOP latch is active.
    pub estop_active: bool,
    /// Any latched fault is active.
    pub latched_fault: bool,
    /// A host AUTO request that is verified, fresh, and wants AUTO.
    pub host_auto_request: bool,
    /// The physical run/enable latch is engaged.
    pub physical_run: bool,
    /// Policy permits AUTO in this build/run mode.
    pub allow_auto: bool,
}

/// Resolve mode, power, and clamp. Host auto requires physical run AND policy.
pub fn resolve_authority(inputs: ModeInputs) -> Authority {
    if inputs.estop_active || inputs.latched_fault {
        return Authority {
            mode: Mode::Manual,
            power: Power::Off,
            clamp: Clamp::Estop,
        };
    }
    if inputs.physical_run && inputs.host_auto_request && inputs.allow_auto {
        Authority {
            mode: Mode::Auto,
            power: Power::On,
            clamp: Clamp::None,
        }
    } else {
        Authority {
            mode: Mode::Manual,
            power: if inputs.physical_run {
                Power::On
            } else {
                Power::Off
            },
            clamp: Clamp::None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Exhaustive over all boolean combinations of the inputs.
    #[test]
    fn host_auto_never_overrides_physical_manual() {
        for estop in [false, true] {
            for latched in [false, true] {
                for host_auto in [false, true] {
                    for physical_run in [false, true] {
                        for allow_auto in [false, true] {
                            let authority = resolve_authority(ModeInputs {
                                estop_active: estop,
                                latched_fault: latched,
                                host_auto_request: host_auto,
                                physical_run,
                                allow_auto,
                            });
                            // ESTOP or latched fault always clamps to MANUAL/off.
                            if estop || latched {
                                assert_eq!(authority.mode, Mode::Manual);
                                assert_eq!(authority.power, Power::Off);
                                assert_eq!(authority.clamp, Clamp::Estop);
                                continue;
                            }
                            // Host AUTO only wins with physical run AND policy.
                            let auto_expected = host_auto && physical_run && allow_auto;
                            if auto_expected {
                                assert_eq!(authority.mode, Mode::Auto);
                                assert_eq!(authority.power, Power::On);
                            } else {
                                assert_eq!(authority.mode, Mode::Manual);
                                // No physical run => power off.
                                assert_eq!(
                                    authority.power,
                                    if physical_run { Power::On } else { Power::Off }
                                );
                            }
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn manual_power_tracks_run_latch() {
        let on = resolve_authority(ModeInputs {
            estop_active: false,
            latched_fault: false,
            host_auto_request: false,
            physical_run: true,
            allow_auto: true,
        });
        assert_eq!(on.mode, Mode::Manual);
        assert_eq!(on.power, Power::On);
    }
}
