# E-Trike System Architecture

Five ECUs on three CAN buses: Jetson Orin (ROS 2 perception), RT ESP32-S3 (realtime physics + steering + CAN gateway), SYS ESP32-S3 (safety + body), MTR STM32 (motor actuation), PWT ESP32-S3 (powertrain gateway).

> Full CAN catalog, processing summaries, pseudocode, and ASCII topology preserved in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 1. Topology

Three physical CAN buses, two gateways:

- **High-level CAN (500 kbit/s):** Jetson ↔ RT. Planning commands (0x300), telemetry (0x210, 0x310, 0x311), heartbeat (0x7FC/0x7FD).
- **Low-level CAN (500 kbit/s):** RT, SYS, PWT, EPS-C (steering), SEB (brake). Actuator commands, safety status, mode control, motor feedback.
- **Powertrain CAN (250 kbit/s):** PWT, DC-DC converter, motor controller. Power control, motor telemetry.

**RT bridges** selected messages between high and low buses. **PWT bridges** selected messages between low and powertrain. Both follow a three-category model: transparent forward, consumed→regenerated, bus-local.

- **Actuators:** Steering (EPS-C via 0x169), brake (SEB via 0x7B9). Mode-gated dual control: RT commands both in AUTO; SYS commands SEB in MANUAL/ESTOP.
- **Motor:** MTR STM32 drives analog throttle (MCP4725 DAC 0–5V) and gear relays (72V). CAN telemetry-only to motor controller.
- **DC-DC converter:** SYS commands enable (0x012), PWT bridges to powertrain bus.

> See [`docs/wiring-harness.md`](docs/wiring-harness.md) for pin-level wiring.

---

## 2. CAN Message Catalog

See [`can-dictionary.md`](can-dictionary.md) for the full bit-level catalog.

Key architectural IDs:

| Bus | ID | Name | Purpose |
|-----|-----|------|---------|
| High | `0x300` | HOST_DRIVE_CMD | Jetson → RT: speed, yaw, gear |
| High | `0x301` | HOST_BRAKE_REQ | Jetson → RT: brake kPa |
| High | `0x302` | HOST_LIGHT_CMD | Jetson → RT (→ SYS): lights |
| High | `0x400` | HOST_OBSTACLE_DIST | Jetson → RT: min obstacle mm |
| High | `0x210` | RT_STATE_RPT | RT → Jetson: mode, steer valid, reversing |
| High | `0x310` | STEER_DIAG | RT → Jetson: steering telemetry |
| High | `0x311` | BRAKE_DIAG | RT → Jetson: brake telemetry |
| Low | `0x001` | SAFETY_ESTOP | Any → All: emergency stop (bridged) |
| Low | `0x011` | SYS_SAFETY_STS | SYS → RT (→ Jetson): estop, hb, lights |
| Low | `0x110` | SYS_MODE_CMD | SYS → RT: mode (Manual/Auto/Estop) |
| Low | `0x204` | RT_DRIVE_CMD | RT → MTR, SYS: speed + gear |
| Low | `0x205` | RT_BRAKE_CMD | RT → SYS: brake kPa |
| Low | `0x169` | VCU_SES_REQ | RT → EPS-C: steering angle |
| Low | `0x201` | SES_STATUS | EPS-C → RT: angle feedback |
| Low | `0x7B9` | VCU_SEB_REQ | RT (AUTO) / SYS (MANUAL/ESTOP) → SEB: brake |
| Low | `0x721` | SEB_STATUS | SEB → SYS: stroke feedback |
| Low | `0x206` | MTR_MOTOR_FBK | MTR → SYS, RT: speed, gear, faults |
| Low | `0x7FD` | RT_HEARTBEAT | RT → SYS, Jetson: alive counter |
| Low | `0x7FE` | SYS_HEARTBEAT | SYS → RT: alive counter |
| Low | `0x7FB` | PWT_HEARTBEAT | PWT → RT, SYS: alive counter |

> Full forwarding rules, per-ECU receive/send tables, and ID-to-bus mapping in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 3. Mode State Machine

```
MANUAL ←→ AUTO ←→ ESTOP
   ↑                 │
   └──START button────┘
```

| Mode | Behavior |
|------|----------|
| **MANUAL** | Rider steers, throttle grip → MTR pass-through → motor. Brake lever → SYS → SEB. EPS-C standalone. DC-DC on. |
| **AUTO** | Jetson 0x300 → RT kinematics → 0x204 (speed) + 0x169 (steering). Lights from Jetson via 0x302. Brake via 0x7B9. |
| **ESTOP** | DAC=0V, gear OFF, steering ramps to 0° at 20°/s, brake=max. DC-DC stays ON. Exit: START button or mode long-press (3s). |

---

## 4. Signal Flow

### Manual Mode

```
Throttle grip → MTR ADC → MTR DAC → Motor controller
Gear selector → TLP281 opto → MTR GPIO → relays → ECU
Brake lever → SYS GPIO → 0x7B9 → SEB
Steering wheel → EPS-C standalone (RT monitors 0x201)
```

### Auto Mode

```
Jetson 0x300 → RT kinematics → 0x204 {speed,gear} → MTR → Motor
                              → 0x169 {angle} → EPS-C
Jetson 0x301 → RT brake arbitration → 0x205 → SYS → 0x7B9 → SEB
Jetson 0x302 → RT forward → 0x302 → SYS → lights
```

---

## 5. Responsibility Split

| Concern | Jetson | RT | SYS | MTR | PWT |
|---------|--------|-----|-----|-----|-----|
| Perception / planning | ✓ | | | | |
| ROS 2 → CAN bridge | ✓ | | | | |
| CAN gateway (high ↔ low) | | ✓ | | | |
| CAN gateway (low ↔ powertrain) | | | | | ✓ |
| Tricycle kinematics | | ✓ | | | |
| Steering compute + CAN TX | | ✓ | | | |
| Steering safety (clamp, following error) | | ✓ | | | |
| Obstacle speed limit | | ✓ | | | |
| Command staleness watchdog | | ✓ | | | |
| ESTOP GPIO + button | | | ✓ | ✓ | |
| Brake lever → CAN | | | ✓ | | |
| DC-DC converter control | | | ✓ | | ✓ |
| Heartbeat monitoring | | ✓ | ✓ | | ✓ |
| Mode switch | | | ✓ | | |
| Throttle ADC / DAC / gear I/O * | | | | ✓ | |
| Motor feedback CAN TX | | | | ✓ | |
| Lights / indicators / 12V relay | | | ✓ | | |
| System diagnostics | | | ✓ | | |

> * Motor I/O currently on SYS; target is MTR STM32 (migration pending hardware).

---

## 6. Design Principles

1. **Queues over shared state.** No mutexes. Thread-safe FreeRTOS queue pipes.
2. **ESTOP bypasses queues.** Safety task preempts and writes directly to actuators.
3. **One CAN ID = one sender per bus.** Heartbeats use per-node IDs (0x7FD RT, 0x7FE SYS, 0x7FC Jetson).
4. **Lower CAN ID = higher bus priority.** Safety IDs (0x00X) win arbitration.
5. **All multi-byte CAN fields big-endian** unless SYNTREE protocol specifies otherwise.
6. **Manual mode is pass-through, not dead.** SYS mirrors physical inputs to outputs.
7. **Actuators are standalone CAN modules.** Commanded via CAN, no direct MCU GPIO.
8. **RT is the only dual-bus node.** No direct Jetson ↔ SYS CAN path.
9. **Listen Before Speaking.** Actuators require status feedback before commands begin.
10. **EGAS 3-level motor safety.** MTR (STM32) = L1 function controller, SYS = L2 monitor, hardwired ESTOP = L3.
11. **Mode-gated dual control (Option D).** RT commands both steering and brake in AUTO (1-hop from kinematics). SYS commands brake in MANUAL/ESTOP. No single MCU failure takes both actuators.

> Full rationale, Options A-D comparison, and EGAS architecture detail in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 7. RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

**Role:** Converts ROS 2 motion commands (0x300) into motor speed+gear (0x204) and steering angle (0x169). Bridges selected CAN messages between high and low buses. Monitors steering feedback and system liveness.

**Hardware:** ESP32-S3 @ 240 MHz, 8 FreeRTOS tasks, dual CAN (TWAI + MCP2515 SPI), 5 encoder inputs (PCNT quadrature).

**Key pins:**

| Signal | GPIO | Notes |
|--------|------|-------|
| CAN TX/RX (low) | 5, 4 | Built-in TWAI |
| SPI SCK/MOSI/MISO/CS/INT | 36–40 | MCP2515 (high CAN) |
| Encoders (rear, front, wheels) | 1–14 | Quadrature PCNT |
| WDT toggle | 21 | TPS3850 at 100 Hz |

**Error handling:**

| Failure | Response |
|---------|----------|
| Low CAN bus-off | Log, auto-recover; ESTOP if persistent |
| High CAN bus-off | Log, auto-recover; zero setpoints |
| Command stale (500ms) | Zero 0x204 + stop 0x169 |
| Steering following error | ESTOP |
| Gateway queue full | Drop (except 0x001 — always forwarded) |

> Full task layout, processing tables, kinematics pseudocode, and config constants in [`docs/architecture-reference.md`](docs/architecture-reference.md) and source at `rt-esp32/src/`.

---

## 8. SYS ESP32-S3 — Safety & Body Control

**Role:** Vehicle safety (ESTOP, brake lever, RT heartbeat), motor actuation (DAC, gear relays via `#ifdef SYS_OWNS_MOTOR`), brake control (SEB via 0x7B9), DC-DC converter, signal lights, mode indicators, diagnostics.

**Hardware:** ESP32-S3 @ 240 MHz, 15 FreeRTOS tasks, built-in TWAI (low CAN only), I2C DAC (MCP4725), GPIO for gear relays, lights, mode button.

**Key pins:**

| Signal | GPIO | Notes |
|--------|------|-------|
| CAN TX/RX | 5, 4 | Built-in TWAI |
| ESTOP button | 1 | NC, active-low, wired to both SYS and MTR |
| Brake lever | 2 | Active-low |
| I2C SDA/SCL | 10, 11 | MCP4725 DAC |
| Gear relays (D/S/R) | 33, 34, 35 | 72V via relay module |
| Lights (turn/brake/head) | 3, 6, 7 | GPIO → relay |
| Mode button | 8 | Manual/Auto toggle |
| WDT toggle | 23 | TPS3850 at 20 Hz |

**Error handling:**

| Failure | Response |
|---------|----------|
| CAN bus-off | Log, auto-recover; ESTOP if persistent |
| RT heartbeat timeout | ESTOP via 0x001 |
| MTR ESTOP ACK timeout | Log + retrigger ESTOP + persistent fault |
| 0x204 staleness (200ms) | Zero speed + Neutral |
| Brake following error | ESTOP |
| ESTOP GPIO | Immediate: DAC=0, gear OFF, brake=max |

> Full task layout, processing tables, and config constants in [`docs/architecture-reference.md`](docs/architecture-reference.md) and source at `sys-esp32/src/`.

---

## 9. Bench Bypass & Debug Tool

### 9.1 Bench Testing Without Full Hardware

The system has hardware dependencies on peer ECUs and SYNTREE actuators. For
bench testing with a single ESP32-S3 and a CAN bus analyzer, compile-time
bypass flags disable these dependencies:

| Flag | Effect | ECU |
|------|--------|-----|
| `CONFIG_BENCH_SOLO` | Disable cross-ECU heartbeat timeouts. Single board won't ESTOP. | RT, SYS |
| `CONFIG_BYPASS_EPS_C_SYNC` | Skip EPS-C listen-sync. Steering assumes centered (0°). | RT |
| `CONFIG_BYPASS_SEB_SYNC` | Skip SEB listen-sync. Brake operates in DEGRADED (lever-only). | SYS |
| `CONFIG_BYPASS_MTR_ABSENT` | Skip EGAS L2 speed monitoring. No MTR feedback required. | SYS |

These flags are in `platformio.ini` `build_flags`. Remove all `CONFIG_BENCH_*`
and `CONFIG_BYPASS_*` before vehicle deployment.

### 9.2 Debug Tool — Synthetic Peer ECUs

When bypass flags aren't sufficient (e.g., testing EGAS L2 with realistic MTR
data), the debug tool can inject synthetic CAN frames to simulate absent peers:

| Peer | Frame | Rate | What it simulates |
|------|-------|------|-------------------|
| EPS-C | 0x201 SES_STATUS | 100 ms | Centered, aligned. RT steering syncs. |
| SEB | 0x721 SEB_STATUS | 100 ms | Aligned, 0mm stroke. SYS brake syncs. |
| MTR | 0x206 MTR_MOTOR_FBK | 50 ms | Speed feedback. SYS EGAS L2 has data. |
| SYS | 0x7FE SYS_HEARTBEAT | 100 ms | SYS alive. RT heartbeat monitor satisfied. |
| RT | 0x7FD RT_HEARTBEAT | 500 ms | RT alive. SYS heartbeat monitor satisfied. |
| Host | 0x300 HOST_DRIVE_CMD | 100 ms | Drive commands. RT generates 0x204/0x169. |
| Host | 0x7FC HOST_HEARTBEAT | 500 ms | Host alive. RT heartbeat monitor satisfied. |

The debug tool has 12 injection templates (was 6). Templates are convenience
presets — the tool can inject any CAN frame via the encode API.

### 9.3 Bench Test Configurations

**Minimal bench (1 ECU + CANalyst-II):**
- Enable `CONFIG_BENCH_SOLO` + all `CONFIG_BYPASS_*` flags
- CANalyst-II injects Host frames (0x300, 0x7FC) on high bus
- No peer ECUs, no actuators

**Full bench (2 ECUs + CANalyst-II):**
- RT + SYS on low bus, CANalyst-II as Host on high bus
- Enable `CONFIG_BENCH_SOLO`, disable actuator bypasses
- CANalyst-II injects synthetic peer frames as needed

---

## 10. CAN Bus Device Maps

**Low-level (500 kbit/s):** RT, SYS, PWT, EPS-C, SEB

**High-level (500 kbit/s):** Jetson, RT

**Powertrain (250 kbit/s):** PWT, DC-DC converter, motor controller (telemetry-only)

> Full per-bus CAN ID tables in [`docs/architecture-reference.md`](docs/architecture-reference.md).

---

## 10. Hardware Summary

- **Motor controller:** Analog throttle 0–5V (MCP4725), gear 72V relays, CAN telemetry-only
- **EPS-C:** Steer-by-wire, 0x169 command @ 50 Hz, 0x201 feedback @ 100 Hz
- **SEB:** Electro-hydraulic brake, 0x7B9 command @ 50 Hz, 0x721 feedback @ 100 Hz
- **DC-DC converter:** 72V→12V, CAN enable via 0x012
- **Power:** 72V traction battery, 12V rail from DC-DC for MCUs + transceivers
- **Watchdog:** TPS3850 external on each MCU, toggled at 20–100 Hz

---

## 11. Build

All firmware builds with PlatformIO (`pio run`).

| ECU | Board | Framework | Key Flags |
|-----|-------|-----------|-----------|
| RT | esp32-s3-devkitc-1 | espidf | 240 MHz, TWAI + SPI CAN |
| SYS | esp32-s3-devkitc-1 | espidf | `-D SYS_OWNS_MOTOR` (bench) |
| MTR | stm32 (TBD) | stm32cube | HAL stubs (migration pending) |
| PWT | esp32-s3-devkitc-1 | espidf | 250k CAN |

---

## 12. Known Design Gaps

| # | Gap | Status |
|---|-----|--------|
| 1 | MTR STM32 HAL — all 4 modules have HAL calls written; missing CubeMX `.ioc` + board | Blocked on hardware |
| 2 | Rear motor + wheel encoders — PCNT code complete; pins now defined in `config.h`; sensors TBD | Blocked on hardware |
| 3 | Steering angle offset hardcoded (no runtime calibration guard) | Code review needed |
| 4 | DLC/range validation — 6 of 16 `from_frame()` now validate; 10 remain | Partial — ongoing |
| 5 | Single-task watchdog — RT has per-task tracking; SYS still single-task WDT | Partial — SYS pending |
| 6 | CAN RX overflow on SYS — counted but not reported on CAN (RT reports in 0x210) | Partial — telemetry pending |
| 7 | 3s startup grace period masks all safety checks; ECUs unsynchronized | Documented — hardware safe |
| 8 | 0x7B9 dual-sender has ~20ms race window on mode switch | Tolerable — CAN arbitration handles |

---

## 13. Reference Documents

- [`can-dictionary.md`](can-dictionary.md) — Full CAN signal catalog
- [`docs/architecture-reference.md`](docs/architecture-reference.md) — Detailed tables, pseudocode, processing summaries
- [`docs/wiring-harness.md`](docs/wiring-harness.md) — Pin-level wiring
- [`docs/hil-safety-test-plan.md`](docs/hil-safety-test-plan.md) — HIL test scenarios
- [`docs/can-bench-test.md`](docs/can-bench-test.md) — Bench test plan
- [`shared/can/can_protocol.h`](shared/can/can_protocol.h) — C++ CAN message structs
