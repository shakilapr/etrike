# FMEA Light — Brake, Steer, Motor Paths

Top 15 failure modes identified through code audit. Each traced from fault →
detection → system response. For full ASIL FMEA, expand to all CAN frames,
all modes, and add quantitative failure rates.

## Brake Path

| # | Failure Mode | Detection | Response | Safe State |
|---|-------------|-----------|----------|------------|
| B1 | SEB CAN comm loss (>20ms) | SEB internal watchdog | SEB locks current position (fail-safe) | Brake holds |
| B2 | SYS 0x7B9 stuck (CAN TX full) | send_can() returns false, logged | Retry once. If persistent, ESP_LOGE | Hardware ESTOP GPIO (Level 3) |
| B3 | RT 0x205 not reaching SYS | SYS heartbeat monitor (0x7FD timeout 1000ms) → ESTOP | SYS brake task uses lever default | Rider lever overrides CAN |
| B4 | SYS heartbeat loss → no 0x7B9 | RT takeover: sends 0x7B9 max stroke directly | RT 0x7B9 continues until SYS recovers | SEB has two senders |
| B5 | Brake lever sensor stuck HIGH | SYS always commands 15mm stroke | Vehicle brakes continuously | Rider can power off (ignition) |

## Steer Path

| # | Failure Mode | Detection | Response | Safe State |
|---|-------------|-----------|----------|------------|
| S1 | EPS-C CAN comm loss (>20ms) | EPS-C internal watchdog | EPS-C locks current angle | Steering holds |
| S2 | 0x169 checksum corruption | steer-by-wire checksum check in EPS-C | EPS-C rejects frame, holds last valid | 3 consecutive fails → EPS-C fault |
| S3 | RT steering follow-error > threshold | RT safety_monitor.h: compares cmd vs actual | ESTOP after 300ms persistence | Steering ramps to 0° at 20°/s |
| S4 | EPS-C angle sensor fault (L3) | 0x202 SES_ErrInfo → RT → ESTOP | RT detects L3 bits, triggers ESTOP | Hardware ESTOP kills motor |
| S5 | RT steering control crash | SYS heartbeat monitor → ESTOP | SYS takes over brake, mode→ESTOP | EPS-C internal hold on comm loss |

## Motor Path

| # | Failure Mode | Detection | Response | Safe State |
|---|-------------|-----------|----------|------------|
| M1 | MCP4725 DAC I2C failure | g_dac.write() returns false | ESP_LOGE, rely on HW ESTOP GPIO | Motor controller sees 0V (DAC power-up default) |
| M2 | MTR CAN comm loss (no 0x206) | SYS: 200ms staleness → zero setpoint + N gear | SYS forces speed=0, gear=N | Motor coasts |
| M3 | EGAS L2 speed mismatch | SYS: abs(cmd - actual) > 500mm/s for 500ms | ESTOP via CAN 0x001 | Hardware ESTOP GPIO kills motor |
| M4 | MTR ESTOP ACK timeout | SYS: 100ms after ESTOP, no ESTOP_ACTIVE bit | Retrigger ESTOP, set persistent fault | HW GPIO path is primary kill |
| M5 | Gear relay stuck energized | Gear conflict detection (multiple HIGH) | fallback to N | Motor in neutral |

## Detection Coverage

All 15 failures have a detection mechanism. 13 have an automatic safe-state
response. 2 (B5 brake stuck, M5 gear stuck) require rider intervention to
reach safe state (power off). The hardware ESTOP GPIO (Level 3) provides
the ultimate backstop for all motor-related failures.
