# Phase 1 Software Validation Traceability

## Requirements
- The software MUST build for both native and esp32 targets.
- All mock CAN components MUST accurately reflect physical boundaries.
- All hardware boundaries MUST be simulated with noise injection.
- CI gate MUST validate static properties.

## Coverage
- `test_pio_native`: basic stubbing framework
- `test_storage_failure`: filesystem isolation testing
- `test_virtual_can_faults`: tests missing CAN frames, malformed bits, and CAN drift
- `can-bit-timing.test.ts`: test limits on generated bit timings
- `invariant-checker.ts`: verify properties holding during physical bounds testing

All these requirements are tested by `tools/phase1-software-gate.ps1`.
