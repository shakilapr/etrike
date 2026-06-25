# MTR STM32 Migration Plan

## Why

EGAS Level 1 requires freedom from interference between the function controller (motor actuation) and the function monitor (safety supervision). Currently, SYS ESP32-S3 handles both actuation (MCP4725 DAC, throttle ADC, gear relays) and monitoring (CAN 0x206 comparison), violating this separation. Moving actuation to the dedicated MTR STM32 board restores the EGAS 3-level architecture:

| Level | Controller | Role |
|-------|-----------|------|
| L1 — Function Controller | MTR STM32 | Motor actuation (target) |
| L2 — Function Monitor | SYS ESP32-S3 | Compare commanded vs actual, trigger ESTOP on mismatch |
| L3 — Hardware Path | Direct ESTOP wire | MTR GPIO cuts MCP4725 + relays independently of CAN/software |

## Current State

| Component | Status |
|-----------|--------|
| SYS ESP32-S3 motor task | Working — I2C (MCP4725), ADC (throttle), GPIO (gear relays) fully implemented |
| MTR STM32 task skeleton | Complete — correct state machines, CAN protocol handling (RX 0x110, 0x204; TX 0x206) |
| MTR STM32 HAL drivers | Stubbed — I2C, ADC, GPIO HAL calls exist but are no-ops |
| Architecture docs | Updated — §5 table footnote, io-data.md sections 3/4, this document |

## Prerequisites

Implement STM32 HAL driver layer for:

- **I2C** — MCP4725 DAC (Nucleo I2C1, SDA=PB7, SCL=PB6). Requires HAL_I2C_Master_Transmit with 400 kHz fast-mode.
- **ADC** — Throttle position (Nucleo ADC1_IN0, PA0). 12-bit, continuous conversion, DMA or interrupt-driven at 100 Hz.
- **GPIO** — Gear relays (forward, reverse, engage, park). Direct HAL_GPIO_WritePin to opto-isolated relay module.
- **ESTOP GPIO** — Direct ESTOP input (shared with SYS). EXTI interrupt or polling at 100 Hz.

Hardware reference: [`docs/wiring.md`](wiring.md) §4 for MTR STM32 pin assignments.

## Migration Steps

### Step 1 — Implement HAL drivers

Implement each HAL driver on MTR STM32 independently, starting with the simplest:

1. **GPIO** — Gear relay outputs. Straightforward digital writes, no timing constraints beyond 100 Hz.
2. **I2C** — MCP4725 DAC. Verify with Nucleo-to-logic-analyzer or oscilloscope that the DAC outputs correct voltage for a given setpoint.
3. **ADC** — Throttle position. Verify raw ADC counts map to the expected 0-5V range. Test at several throttle grip positions.
4. **ESTOP GPIO** — Confirm that physical ESTOP button pulls the MTR GPIO low and that firmware sets DAC=0V + relays OFF within 10 ms.

### Step 2 — Bench-test each I/O independently

Before any CAN integration, verify each peripheral on the bench:

- MCP4725: Set DAC to N volts, measure with multimeter.
- ADC: Apply known voltage to PA0, confirm reading.
- Gear relays: Toggle each relay, confirm audible click + 72V continuity.
- ESTOP: Press button, confirm GPIO state change.

### Step 3 — Run MTR alongside SYS in shadow mode

SYS continues to actuate motor I/O. MTR receives the same CAN inputs (0x110, 0x204) and drives identical I/O in parallel, but SYS remains the sole physical actuator. Compare MTR output values against SYS via:

- CAN 0x206 feedback (MTR publishes actual_speed_mmps, gear_state).
- Debug serial logging from both boards.
- No physical coupling yet — MTR I/O outputs are disconnected during this step.

### Step 4 — Cut over to MTR actuation

1. Physically disconnect MCP4725 DAC, throttle ADC, and gear relays from SYS.
2. Connect them to MTR STM32.
3. Enable MTR actuation (clear a firmware gate that defers to SYS).
4. Verify full MANUAL mode operation: throttle grip → ADC → MCP4725 → motor speed.
5. Verify full AUTO mode operation: CAN 0x204 → MCP4725 → motor speed.
6. Verify ESTOP: button press → DAC=0V, all relays OFF within 10 ms.
7. Leave SYS motor task compiled but idle (no physical I/O) as a hot spare.

### Step 5 — Verify EGAS L1/L2 separation with fault injection

| Fault | Expected behavior |
|-------|------------------|
| MTR I2C bus fault (MCP4725 NACK) | DAC output defaults to 0V. SYS detects stalled 0x206 → triggers ESTOP via CAN 0x001. |
| MTR ADC fault (throttle stuck) | MTR detects stuck value → enters safe state (DAC=0V). SYS sees 0x206 mismatch → CAN ESTOP. |
| MTR firmware crash | 0x206 stops. SYS 0x206 watchdog times out → CAN ESTOP + local motor kill on SYS (if still wired). |
| SYS firmware crash | MTR continues independently (MCP4725 + relays). ESTOP input still works on MTR GPIO. |
| CAN bus loss | MTR detects bus-off → DAC=0V, relays OFF. SYS independently enters safe state. |
| ESTOP button press | Both MTR GPIO + SYS GPIO see the level change simultaneously. MTR kills motor locally, SYS broadcasts 0x001. |

## Verification

- **Independent power domains:** SYS and MTR must be on separate 5V regulators (or separate LDOs on the same 12V rail with independent decoupling). Verify that shorting one board's 5V rail does not affect the other's.
- **ESTOP kills both paths:** Physical ESTOP button connects to both SYS GPIO1 and MTR kEstopGpio via normally-closed wiring. Verify with button pressed: MCP4725 output = 0V (measured), all gear relays OFF (audible click + continuity check).
- **CAN watchdog catches MTR freeze:** SYS monitors 0x206 at 10 Hz. If 0x206 stops for 300 ms, SYS must broadcast CAN 0x001 and enter ESTOP state. Verify by halting MTR firmware via debugger and confirming SYS ESTOP within 300 ms.

## Architecture Gap Reference

This migration is tracked as **architecture gap #5**. See [`architecture.md`](../architecture.md) §5 footnote for the responsibility table note.
