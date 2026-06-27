# E-Trike System Architecture

Five-node distributed control: **Jetson Orin** (ROS 2 perception/planning), **RT ESP32-S3** (realtime physics, steering, brake & CAN gateway), **SYS ESP32-S3** (safety & body control), **MTR STM32** (motor actuation), **PWT ESP32-S3** (powertrain CAN gateway).

> **Variant:** A consolidated single-controller version exists at [`rt-aurix-lite/`](rt-aurix-lite/) — combines RT+SYS into one AURIX TC3xx on a single CAN bus. This document describes the distributed reference architecture. Both variants share the same CAN protocol and actuator interfaces.

Three physical CAN buses: two at 500 kbit/s (high-level and low-level) and one at 250 kbit/s (powertrain). RT bridges selected messages between high and low buses. PWT bridges selected messages between low and powertrain buses. Actuators are **SYNTREE** CAN modules: EPS-C (steer-by-wire) and SEB (electro-hydraulic brake). **Mode-gated dual control (Option D):** RT directly commands both EPS-C and SEB in AUTO mode; SYS commands SEB in MANUAL mode (lever pass-through) and on ESTOP. This ensures no single MCU failure takes both actuators, and AUTO-mode brake+steer are 1-hop from the kinematics engine. Motor control is on a dedicated STM32 board (MTR) for safety isolation per ISO 26262 EGAS 3-level concept.

> **Autoware.Auto v0.0.4-alpha:** A new ROS 2 node on the Jetson (`autoware_vehicle_bridge`) provides Autoware.Auto topic compatibility (`AckermannControlCommand` in, `VelocityReport`/`SteeringReport` out). The CAN protocol across both buses is unchanged. See §5.1.
>
> **PWT variant:** The powertrain CAN gateway (PWT ESP32-S3) is detailed in [`pwt-esp32/pwt-architecture.md`](pwt-esp32/pwt-architecture.md).

---

## 1. Topology

```
  ┌────────────────── High-Level CAN (500 kbit/s) ──────────────────┐
  │                                                                  │
  │  ┌──────────┐            ┌──────────────┐                       │
  │  │  Jetson  │            │  RT ESP32-S3 │                       │
  │  │  Orin    │            │              │                       │
  │  │          │            │ Physics      │                       │
  │  │ ROS 2    │            │ Steering     │                       │
  │  │ Planning │            │ Gateway      │                       │
  │  └────┬─────┘            │              │                       │
  │       │                  └──────┬───────┘                       │
  │  TX:  0x300,0x301,    TX: 0x011,0x120, │                        │
  │       0x302,0x001,          0x210,0x220,│                        │
  │       0x400,0x7FC           0x206,0x310,│                        │
  │                              0x311,0x600,│                       │
  │  RX:  0x011,0x120,          0x001,0x7FC │                        │
  │       0x206,0x210,      RX: 0x300,0x301,│                        │
  │       0x220,0x310,          0x302,0x001,│                        │
  │       0x311,0x600,          0x400,0x7FC │                        │
  │       0x001,0x7FD                       │                        │
  └─────────────────────────────────────────┘                        │
                                            │                        │
                 ┌──────────────────────────┘                        │
                 │                                                    │
  ┌──────────────▼─────── Low-Level CAN (500 kbit/s) ───────────────┐│
  │                                                                  ││
  │  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐        ││
  │  │  RT ESP32-S3 │   │ SYS ESP32-S3 │   │ PWT ESP32-S3 │        ││
  │  │  (gateway)   │   │              │   │              │        ││
  │  └──────┬───────┘   │ Safety       │   │ Powertrain   │        ││
  │         │           │ Motor        │   │ Gateway      │        ││
  │    TX:  0x169,      │ Brake        │   └──────┬───────┘        ││
  │         0x204,      └──────┬───────┘          │                 ││
  │         0x205,             │            TX:  0x7FB (HB)        ││
  │         0x302,        TX:  0x7B9,      RX:  0x001,0x012,      ││
  │         0x001,             0x011,            0x7FD,0x7FE       ││
  │         0x7FD              0x012,0x110,                        ││
  │    RX:  0x001,             0x120,0x600,                        ││
  │         0x011,             0x001,0x7FE                         ││
  │         0x110,0x120,   RX:  0x001,0x204,                       ││
  │         0x201,0x206,        0x205,0x302,                       ││
  │         0x600,0x7FD,        0x721,0x731,                       ││
  │         0x7FE               0x741,0x6FB,                       ││
  │                              0x7FD,0x7FB                       ││
  └──────────────────────────────────────────────────────────────────┘│
                                        │                             │
                ┌───────────────────────┼────────────────────┐        │
                │                       │                    │        │
          ┌─────▼─────┐          ┌─────▼─────┐        ┌─────▼─────┐  │
          │  SYNTREE  │          │  SYNTREE  │        │   MTR     │  │
          │  SEB      │          │  EPS-C    │        │  STM32   │  │
          │  (Brake)  │          │ (Steering)│        │ (Motor)  │  │
          │ 0x7B9 cmd │          │ 0x169 cmd │        │ 0x204 cmd │  │
          │ 0x721 stat│          │ 0x201 stat│        │ 0x206 fbk │  │
          └───────────┘          └───────────┘        └───────────┘  │
                                                                     │
  ┌──────────────▼────── Powertrain CAN (250 kbit/s) ───────────────┐│
  │                                                                  ││
  │  ┌──────────────┐                                               ││
  │  │ PWT ESP32-S3 │      ┌───────────┐      ┌───────────┐        ││
  │  │  (gateway)   │      │  DC-DC    │      │  Motor    │        ││
  │  └──────┬───────┘      │ Converter │      │Controller │        ││
  │         │              │ 72V→12V   │      │           │        ││
  │    TX:  0x012,         │ (0x012)   │      │ CAN: TBD  │        ││
  │         0x001          └───────────┘      │ Analog:   │        ││
  │    RX:  TBD (motor)                       │ 0-5V thr  │        ││
  │                                            │ 72V gear  │        ││
  │                                            └───────────┘        ││
  └──────────────────────────────────────────────────────────────────┘
```

> **Motor controller:** Still accepts analog throttle (0–5V via MCP4725) and gear (72V relays) from MTR STM32. CAN interface on the 250k bus is **telemetry-only** (speed, current, temp, faults). Specific CAN IDs TBD. PWT bridges `0x012` transparently (same ID/payload) from 500k low bus to 250k powertrain bus. PWT heartbeat `0x7FB` on 500k low bus only — PWT does not appear on high CAN. Full PWT architecture: [`pwt-esp32/pwt-architecture.md`](pwt-esp32/pwt-architecture.md).

---

## 2. CAN message catalog

### 2.1 Low-level CAN

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Prio |
|----|------|--------|-------------|-----|---------|--------|------|
| `0x001` | SAFETY_ESTOP | Any | All (bridged to high) | 0 | (none) | Event | Highest |
| `0x011` | SYS_SAFETY_STS | SYS | RT (→ Jetson) | 3 | u8 estop, u8 hb_ok, u4 light_state | 5 Hz | V.High |
| `0x012` | SYS_DCDC_CMD | SYS | PWT (→ DC-DC on 250k bus) | 1 | u8 enable | Change | V.High |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | u8 mode (0=M, 1=A, 2=ESTOP) | Change | High |
| `0x120` | SYS_THROTTLE_STS | MTR | RT (→ Jetson) | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x169` | VCU_SES_REQ | RT | EPS-C (steering) | 8 | Angle cmd + security bytes | 50 Hz | Medium |
| `0x201` | SES_STATUS | EPS-C | RT | 8 | Steering angle + status feedback | 100 Hz | Medium |
| `0x202` | SES_ErrInfo | EPS-C | RT | 8 | 25 fault flags (8× L3) | 10 Hz | Medium |
| `0x203` | SES_Version | EPS-C | RT | 8 | SW + HW version | 1 Hz | Lowest |
| `0x204` | RT_DRIVE_CMD | RT | **MTR (STM32), SYS** | 5 | i32 speed_mmps, u8 gear | 100 Hz | Medium |
| `0x205` | RT_BRAKE_CMD | RT | SYS | 4 | i32 brake_pressure_kpa | 50 Hz | Low |
| `0x206` | MTR_MOTOR_FBK | MTR | SYS, RT | 4 | i16 actual_speed, u8 gear_state, u8 fault_flags | 50 Hz | Low |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | SYS | 1 | u8 lights bitfield | Change | Medium |
| `0x600` | SYS_DIAG_RPT | SYS | RT (→ Jetson) | 8 | diag struct | 1 Hz | Lowest |
| `0x6FA` | SES_Test | EPS-C | RT | 8 | Motor current, ECU temp, supply voltage | 100 Hz | Lowest |
| `0x6FB` | SEB_Test | SEB | SYS | 8 | Motor current, ECU temp, supply voltage | 100 Hz | Lowest |
| `0x721` | SEB_STATUS | SEB | SYS | 8 | Brake stroke + status + error level feedback | 100 Hz | Lowest |
| `0x731` | SEB_ErrInfo | SEB | SYS | 8 | 23 fault flags (16× L3) | 10 Hz | Lowest |
| `0x741` | SEB_Version | SEB | SYS | 8 | SW + HW version | 1 Hz | Lowest |
| `0x7B9` | VCU_SEB_REQ | RT (AUTO) / SYS (MANUAL, ESTOP) † | SEB (brake) | 8 | Stroke/pressure cmd + security | 50 Hz | Lowest |
| `0x7FB` | PWT_HEARTBEAT | PWT | RT, SYS | 1 | u8 alive_ctr | 2 Hz | Lowest |
| `0x7FD` | RT_HEARTBEAT | RT | SYS | 1 | u8 alive_ctr | 2 Hz | Lowest |
| `0x7FE` | SYS_HEARTBEAT | SYS | RT | 1 | u8 alive_ctr | 10 Hz | Lowest |

> **ID note**: SYNTREE units are preprogrammed and cannot be reconfigured. EPS-C uses factory command `0x169` and status `0x201`, plus diagnostic frames `0x202` (err info), `0x203` (version), `0x6FA` (telemetry). SEB uses factory command `0x7B9` and status `0x721`, plus diagnostic frames `0x731` (err info), `0x741` (version), `0x6FB` (telemetry). `RT_DRIVE_CMD` is placed at `0x204` to avoid collision with EPS-C `0x169`. `0x205` RT_BRAKE_CMD avoids collision with `0x202` and `0x203`.
>
> **† `0x7B9` dual-sender note:** Architecture §6.2 Option D specifies RT sends `0x7B9` directly in AUTO (1-hop from kinematics), SYS in MANUAL/ESTOP. **Current implementation (v0.0.4):** SYS is the sole `0x7B9` sender in all modes — RT sends `0x205 RT_BRAKE_CMD` to SYS, which converts to SEB protocol. Direct RT→SEB transmission is planned (gap #12).

### 2.2 High-level CAN

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Prio |
|----|------|--------|-------------|-----|---------|--------|------|
| `0x001` | SAFETY_ESTOP | RT (fwd), Jetson | Jetson, RT | 0 | (none) | Event | Highest |
| `0x011` | SYS_SAFETY_STS | RT (fwd) | Jetson | 3 | u8 estop, u8 hb_ok, u4 light_state | 5 Hz | V.High |
| `0x120` | SYS_THROTTLE_STS | RT (fwd) | Jetson | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x206` | MTR_MOTOR_FBK | RT (fwd) | Jetson | 4 | i16 actual_speed, u8 gear_state, u8 fault_flags | 50 Hz | Low |
| `0x210` | RT_STATE_RPT | RT | Jetson | 4 | u8 mode, u8 steer_valid, u8 reversing, u8 rx_overflow | 10 Hz | Low |
| `0x220` | RT_PID_RPT | RT | Jetson | 6 | RESERVED — future closed-loop PID telemetry | — (inactive) | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | i32 speed_mmps, i24 yaw_rate_mrad_s, u8 gear | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQ | Jetson | RT | 4 | i32 brake_pressure_kpa | Demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→ SYS) | 1 | u8 lights bitfield | Change | Medium |
| `0x310` | STEER_DIAG | RT | Jetson | 8 | i16 angle (0.1°/bit, offset=-3000), u8 fault, i16 motor_current, u16 ecu_temp | 10 Hz | Low |
| `0x311` | BRAKE_DIAG | RT | Jetson | 8 | u16 pressure (0.05 MPa/bit), u8 fault, i16 motor_current, u16 ecu_temp | 10 Hz | Low |
| `0x400` | HOST_OBSTACLE_DIST | Jetson | RT | 4 | u32 distance_mm | 10 Hz | Low |
| `0x600` | SYS_DIAG_RPT | RT (fwd) | Jetson | 8 | diag struct | 1 Hz | Lowest |
| `0x7FD` | RT_HEARTBEAT | RT | Jetson | 1 | u8 alive_ctr | 2 Hz | Lowest |
| `0x7FC` | HOST_HEARTBEAT | Jetson | RT | 1 | u8 alive_ctr | 2 Hz | Lowest |

> Bit-level signal layouts in [`can-dictionary.md`](can-dictionary.md).

### 2.3 CAN gateways — message handling by category

Two nodes bridge CAN buses: **RT** (high ↔ low) and **PWT** (low ↔ powertrain). Each gateway follows the same three-category model.

#### 2.3.1 RT gateway (high ↔ low, 500k both sides)

RT is the only high/low bridge. Every CAN message on high or low falls into one of three categories:

#### Category 1: Transparent forward

RT copies the frame to the other bus unchanged — same CAN ID, same payload, same DLC. The receiving node cannot tell whether RT or the original sender transmitted it.

| Direction | IDs forwarded | Example |
|-----------|--------------|---------|
| Low → High | `0x001`, `0x011`, `0x120`, `0x206`, `0x600` | SYS sends `0x011` on low → RT transmits `0x011` on high → Jetson receives |
| High → Low | `0x001`, `0x302` | Jetson sends `0x302` on high → RT transmits `0x302` on low → SYS receives |

> `0x001` is the only bidirectional forward — ESTOP originates on either bus and must reach all nodes on both buses.

#### Category 2: Consumed by RT → different message generated on other bus

RT receives a command on one bus, processes it internally, and transmits a **different** CAN ID with **different** payload on the other bus. These are not forwards — they are translations.

| Inbound (consumed) | Bus | Processing | Outbound (generated) | Bus |
|--------------------|-----|-----------|----------------------|-----|
| `0x300` HOST_DRIVE_CMD | High | Kinematics  → `ResolvedSetpoint` | `0x204` RT_DRIVE_CMD + `0x169` VCU_SES_REQ | Low |
| `0x301` HOST_BRAKE_REQ | High | Max-select arbitration → `0x205` RT_BRAKE_CMD | `0x205` (RT→SYS, AUTO only); SYS converts to `0x7B9` → SEB | Low |

#### Category 3: Bus-local (never forwarded, never regenerated)

These messages serve only nodes on a single bus. RT neither forwards nor translates them.

| Bus | IDs | Reason |
|-----|-----|--------|
| Low only | `0x110`, `0x169`, `0x204`, `0x205`, `0x7B9` | Mode, steering, drive, brake, and SEB commands. RT-generated or SYS-generated — never leave low bus. |
| Low only | `0x012` | DC-DC command. SYS sends on low; PWT bridges transparently to powertrain bus. RT ignores. |
| Low only | `0x201`, `0x202`, `0x203`, `0x6FA` | EPS-C status and diagnostics — RT consumes locally for boot sync, monitoring, and fault handling |
| Low only | `0x721`, `0x731`, `0x741`, `0x6FB` | SEB status and diagnostics — SYS consumes locally for boot sync, monitoring, and fault handling |
| High only | `0x210`, `0x220`, `0x400` | RT telemetry — generated by RT for Jetson consumption only |
| Both (independent) | `0x7FD`, `0x7FE`, `0x7FC` | Per-node heartbeat with alive counter — MUST NOT be bridged (see §8.6). Each bus has its own liveness domain. |

#### 2.3.2 PWT gateway (low ↔ powertrain, 500k ↔ 250k)

PWT bridges selected messages between the 500k low bus and the 250k powertrain bus. Same three-category model:

**Category 1: Transparent forward**
PWT copies the frame unchanged — same CAN ID, same payload, same DLC.

| Direction | IDs forwarded | Example |
|-----------|--------------|---------|
| Low → Powertrain | `0x012`, `0x001` | SYS→0x012 on low → PWT→0x012 on powertrain → DC-DC receives |
| Powertrain → Low | `0x001` | Motor controller sends 0x001 on powertrain → PWT→0x001 on low → all nodes |

**Category 2: Rewrite (consume, generate different ID on other bus)**
Motor controller telemetry on the 250k bus is parsed by PWT and republished as different CAN IDs on the 500k low bus. Specific IDs TBD — depends on motor controller CAN protocol.

**Category 3: Bus-local (never forwarded)**
PWT heartbeat (`0x7FB`) stays on the 500k low bus — never forwarded to powertrain or high CAN. PWT is invisible to high CAN.

### 2.4 Powertrain CAN (250 kbit/s)

Devices: PWT (gateway), DC-DC converter, motor controller. PWT bridges `0x012` and `0x001` from the 500k low bus.

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Notes |
|----|------|--------|-------------|-----|---------|--------|-------|
| `0x001` | SAFETY_ESTOP | PWT (fwd), any | DC-DC, motor controller | 0 | (none) | Event | Forwarded from low bus |
| `0x012` | SYS_DCDC_CMD | PWT (fwd) | DC-DC converter | 1 | u8 enable | Change | Transparent forward from SYS on low bus |
| TBD | Motor controller telemetry | Motor controller | PWT | TBD | Speed, current, temp, faults | TBD | CAN IDs TBD — depends on motor controller model |

> **Motor controller note:** The motor controller outputs telemetry on the 250k bus but still accepts analog throttle (0–5V via MCP4725) and gear (72V relays) from the MTR STM32. Its CAN interface is telemetry-only. Specific CAN IDs and signal layouts are TBD.

---

## 3. Mode state machine

```
         ┌──────────┐
    ┌───▶│  MANUAL  │◀───┐
    │    └─────┬────┘    │
    │     push btn=AUTO  push btn=MANUAL
    │          │          │
    │    ┌─────▼────┐    │
    │    │   AUTO   │    │
    │    └─────┬────┘    │
    │          │          │
    │  ESTOP button / CAN 0x001 / HB timeout
    │          │          │
    │    ┌─────▼────┐    │
    │    │  ESTOP   │────┘
    │    └─────┬────┘
    │         │ START button (GPIO32)
    │         ▼
    └──────── MANUAL
```

| Mode | Behavior |
|------|----------|
| **MANUAL** | Rider steers / rides throttle. **MTR** reads throttle ADC + gear sense → pass-through via MCP4725 + relays. Brake lever → SYS GPIO → CAN `0x7B9` → SEB. EPS-C standalone (RT idle). DC-DC on. Mode gated by SYS `0x110`. |
| **AUTO** | Jetson `AckermannControlCommand` → high CAN `0x300` → RT kinematics → low CAN `0x204` (MTR: speed+gear) + `0x169` (EPS-C: angle). **MTR** drives MCP4725 + gear relays following `0x204`. Lights from Jetson via `0x302` (RT fwd). Brake via `0x7B9`. Mode gated by SYS `0x110`. |
| **ESTOP** | MCP4725 = 0 V, all gear outputs OFF, `0x7B9` stroke=max (full brake), steering ramps to 0° at 20°/s via active `0x169` (unless obstacle-triggered → hold then silent-stop), DC-DC ON (`0x012 enable=1` — maintains 12V for MCUs, CAN transceivers, brake light), 12V accessory relay OFF (kills headlight, turn signals, mode bulbs). Brake light ON (powered from always-on DC-DC rail, not through accessory relay). Exit: **START button** or **MODE long-press (3s)** → MANUAL. Steering ramp completes before 0x169 handoff (brake/motor/lights transition immediately; steering defers until centered). Or power-cycle. |

---

## 4. Signal flow

### 4.1 Manual mode

```
Throttle grip (0–5V) ──► MTR ADC ──► MTR MCP4725 (0–5V) ──► Motor controller
Gear selector (72V)  ──► TLP281 opto → MTR GPIO ──► relay module → 72V → ECU
Brake lever           ──► SYS GPIO ──► CAN 0x7B9 → SEB (stroke=MAX if pressed)
Steering wheel        ──► EPS-C standalone (RT idle, monitors 0x201)
Signal lights         ──► Turn: handlebar switches (SYS GPIO3/6). Head: toggle (SYS GPIO7). Brake: OR logic → SYS GPIO21
DC-DC converter       ──► SYS CAN 0x012 → PWT (fwd to 250k) → DC-DC → 12V rail on
SYS → CAN 0x110       ──► MTR receives mode=Manual → ADC pass-through
```

### 4.2 Auto mode

```
Jetson AckermannControlCommand ──► High CAN 0x300 ──► RT kinematics
                                          │
               ┌──────────────────────────┤
               ▼ (low CAN)                ▼ (low CAN)
   0x204 {speed, gear} → MTR       0x169 {angle} → EPS-C
               │                          │
               ├──► MCP4725 → Motor       │  RT listens 0x201 for feedback
               ├──► Relays → ECU gear     │  Dynamic angle clamp + following error
               └──► CAN 0x120, 0x206 →    │
                     RT (fwd to Host),     │
                     SYS (EGAS L2 monitor) │

SYS → CAN 0x110 ──► MTR receives mode=Auto → follows 0x204
Jetson ──► High CAN 0x301 ──► RT brake arbitration → Low CAN 0x7B9 → SEB (RT sends directly in AUTO; planned, gap #12)
Jetson ──► High CAN 0x302 ──► RT fwd → Low CAN 0x302 → SYS → light relays
SYS ────► Low CAN 0x7B9 → SEB (MANUAL/ESTOP only; RT sends in AUTO)
```

---

## 5. Responsibility split

| Concern | Jetson | RT | SYS | MTR | PWT |
|---------|--------|-----|-----|-----|-----|
| Perception / planning | ✓ | | | | |
| ROS 2 → CAN bridge (`AckermannControlCommand`) | ✓ | | | | |
| Autoware.Auto bridge (`autoware_vehicle_bridge`) | ✓ | | | | |
| CAN gateway (low ↔ high) | | ✓ | | | |
| CAN gateway (low ↔ powertrain, 250k) | | | | | ✓ |
| Tricycle kinematics | | ✓ | | | |
| Steering angle compute + CAN TX (`0x169`) | | ✓ | | | |
| Steering boot sync (Listen-Before-Speaking) | | ✓ | | | |
| Steering safety: dynamic angle clamp, hard-stops, following error | | ✓ | | | |
| Obstacle speed limit | | ✓ | | | |
| Command staleness watchdog | | ✓ | | | |
| E-stop GPIO + button (wired to both) | | | ✓ | ✓ | |
| Brake lever → CAN (`0x7B9`, 50 Hz continuous) | | | ✓ | | |
| Brake boot sync (Listen-Before-Speaking) | | | ✓ | | |
| Brake rolling counter + checksum | | | ✓ | | |
| DC-DC converter CAN control (`0x012`) | | | ✓ (send) | | ✓ (bridge to 250k) |
| Motor controller CAN telemetry relay | | | | | ✓ (TBD) |
| Heartbeat monitoring | | ✓ (Jetson high + SYS low) | ✓ (RT, low) | | ✓ (low bus) |
| Mode switch reading | | | ✓ | | |
| Throttle ADC read (0–5V) | | | SYS (ESP32-S3), target MTR (STM32)[1] | | |
| Throttle MCP4725 DAC output (0–5V) | | | SYS (ESP32-S3), target MTR (STM32)[1] | | |
| Gear 72V read (TLP281 opto) | | | SYS (ESP32-S3), target MTR (STM32)[1] | | |
| Gear 72V output (relay module) | | | SYS (ESP32-S3), target MTR (STM32)[1] | | |
| Motor feedback CAN TX (`0x206`) | | | SYS (ESP32-S3), target MTR (STM32)[1] | | |
| 12V accessory power relay | | | ✓ | | |
| Mode indicator lights | | | ✓ | | |
| Signal lights (turn, brake, head) | | | ✓ | | |
| System diagnostics | | | ✓ | | |

> **[1] Motor I/O note:** Motor actuation (MCP4725 DAC, throttle ADC, gear relays) currently runs on SYS ESP32-S3. The dedicated MTR STM32 board has a complete task skeleton with correct state machines but the STM32 HAL driver layer (I2C, ADC, GPIO) is not yet implemented. Migration to MTR is tracked as architecture gap #5. Until migration is complete, EGAS Level 2 monitoring (SYS comparing 0x204 vs 0x206) provides CAN-level staleness detection but not physical speed verification since no wheel encoder is fitted.

---

### 5.1 Jetson Orin — Autoware.Auto ROS 2 Bridge (v0.0.4-alpha)

A new ROS 2 lifecycle node (`autoware_vehicle_bridge`) at `jetson/src/autoware_vehicle_bridge/` provides Autoware.Auto topic compatibility. The CAN protocol on both buses is unchanged.

**Subscriptions:** `AckermannControlCommand` → CAN `0x300`/`0x301`. `GearCommand`, `TurnIndicatorsCommand`, `HazardLightsCommand` → CAN `0x302`. `ControlModeCommand` → Engage state (AUTONOMOUS→engaged, MANUAL→stops commands; physical mode gated by SYS MODE button). `VehicleEmergencyStamped` → CAN `0x001` ESTOP (rate-limited: 1 per 500ms). `Engage` → internal state (false suppresses commands).

**Publications:** `VelocityReport` ← CAN `0x120`. `SteeringReport` ← CAN `0x310`. `GearReport` ← CAN `0x206` (primary: MTR_GearState N/D/S/R) + `0x210` (fallback: reversing flag). `ControlModeReport` ← CAN `0x210` + `0x011`. `TurnIndicatorsReport` / `HazardLightsReport` ← CAN `0x011` byte 2 (SYS light state, open-loop echo fallback until SYS updated). `VehicleKinematicState` ← dead reckoning from CAN `0x120` + `0x310` (drifts without absolute reference; full encoder+IMU odometry deferred to gap #5).

**Heartbeats:** Sends `0x7FC` at 2 Hz. Monitors `0x7FD` (1500ms timeout → RCLCPP_WARN_THROTTLE). RT's own staleness watchdog (500ms) stops the vehicle independently.

**Node parameters:** `wheel_base: 1.5m`, `max_speed_forward: 3.0 m/s`, `max_steering_angle: 0.698 rad`, `max_brake_pressure_kpa: 5000`, `loop_rate: 100 Hz`, `command_timeout_ms: 500`.

#### 5.1.1 Jetson I/O & Processing Summary

The Jetson Orin is the perception, planning, and high-level control node. It runs ROS 2 with the `autoware_vehicle_bridge` lifecycle node translating Autoware.Auto topics ↔ CAN frames. The Jetson has **no direct actuator connections** — all actuation is delegated to RT/SYS/MTR via CAN.

**ROS 2 → CAN (subscriptions → outputs):**

| ROS 2 Topic (subscribe) | Processing | CAN ID (TX) — DLC, fields | Controls |
|--------------------------|------------|---------------------------|----------|
| `AckermannControlCommand` — `{longitudinal.speed, lateral.steering_tire_angle}` | Speed m/s→mm/s, steering_angle→yaw_rate via kinematics (L=1.5m), gear derivation (v>0→D, v=0→N, v<0→R) | `0x300` HOST_DRIVE_CMD — DLC=8, `{i32 speed_mmps, i24 yaw_rate_mrad_s, u8 gear}` | Vehicle motion |
| `AckermannControlCommand` — `{longitudinal.acceleration}` | Deceleration m/s² → brake pressure kPa mapping | `0x301` HOST_BRAKE_REQ — DLC=4, `{i32 brake_pressure_kpa}` | Service brake |
| `GearCommand` — `{gear}` | Gear enum → u8 (N=0, D=1, S=2, R=3) | `0x300` (gear byte within DLC=8 frame) | Gear selection |
| `TurnIndicatorsCommand` / `HazardLightsCommand` — `{command}` | Pack turn L, turn R, hazard, brake, headlight bits into u8 bitfield | `0x302` HOST_LIGHT_CMD — DLC=1, `{u8 lights bitfield}` | Signal lights |
| `ControlModeCommand` — `{mode}` (AUTONOMOUS) | Engages command transmission | Enables `0x300`/`0x301` TX | Mode engagement |
| `ControlModeCommand` — `{mode}` (MANUAL) | Suppresses all commands | Stops all TX | Mode disengagement |
| `VehicleEmergencyStamped` — `{emergency}` | Rate-limited: 1 per 500ms | `0x001` SAFETY_ESTOP — DLC=0 | Emergency stop |
| Perception (LiDAR/camera/stereo) | Minimum obstacle distance mm | `0x400` HOST_OBSTACLE_DIST — DLC=4, `{u32 distance_mm}` (10 Hz) | Obstacle speed limit (RT safety backstop) |
| — | Heartbeat: alive_ctr++ | `0x7FC` HOST_HEARTBEAT — DLC=1, `{u8 alive_ctr}` (2 Hz) | Liveness |

**CAN → ROS 2 (inputs → publications):**

| CAN ID (RX) — DLC, fields | Processing | ROS 2 Topic (publish) | Provides |
|---------------------------|------------|----------------------|----------|
| `0x120` SYS_THROTTLE_STS — DLC=2, `{i16 speed_mmps}` | Speed mm/s → m/s, sign→direction | `VelocityReport` — `{header, longitudinal_velocity, heading_rate}` | Longitudinal velocity |
| `0x310` STEER_DIAG — DLC=8, `{i16 angle, u8 fault, i16 motor_current, u16 ecu_temp}` | Angle 0.1°/bit, offset=-3000 → radians | `SteeringReport` — `{stamp, steering_tire_angle}` | Steering tire angle |
| `0x206` MTR_MOTOR_FBK — DLC=4, `{i16 actual_speed, u8 gear_state, u8 fault_flags}` | gear_state byte → GearReport enum | `GearReport` — `{stamp, report}` | Current gear (primary) |
| `0x210` RT_STATE_RPT — DLC=3, `{u8 mode, u8 steer_valid, u8 reversing}` + `0x011` SYS_SAFETY_STS — DLC=3, `{u8 estop, u8 hb_ok, u4 light_state}` | mode + estop + hb_ok → ControlMode enum | `ControlModeReport` — `{stamp, mode}` | Current control mode |
| `0x011` SYS_SAFETY_STS byte 2 — `{u4 light_state}` | light_state bitfield → indicator topic enum | `TurnIndicatorsReport` / `HazardLightsReport` | Light state (open-loop echo fallback) |
| `0x120` + `0x310` | Dead reckoning: integrate speed + steer angle over time (drifts without absolute reference) | `VehicleKinematicState` — `{stamp, state, pose, twist, accel}` | Odometry (full encoder+IMU fusion deferred to gap #5) |
| `0x600` SYS_DIAG_RPT — DLC=8, `{diag struct}` | Pass-through for logging / diagnostics | Internal diagnostics topic (1 Hz) | System diagnostics |
| `0x7FD` RT_HEARTBEAT — DLC=1, `{u8 alive_ctr}` | 1500ms timeout (3 missed at 2 Hz) → RCLCPP_WARN_THROTTLE | — (internal monitoring) | RT liveness |

**CAN I/O consolidated:**

| Direction | CAN IDs | Rate | Purpose |
|-----------|---------|------|---------|
| TX (6) | `0x300`, `0x301`, `0x302`, `0x400`, `0x001`, `0x7FC` | Various | Commands, obstacle data, ESTOP, heartbeat |
| RX (7) | `0x011`, `0x120`, `0x206`, `0x210`, `0x310`, `0x600`, `0x7FD` | Various | Telemetry, diagnostics, liveness |

**Heartbeats:** Sends `0x7FC` at 2 Hz. Monitors `0x7FD` — 1500ms timeout → RCLCPP_WARN_THROTTLE (RT's own 500ms staleness watchdog stops vehicle independently).

**What the Jetson manipulates:** Autoware.Auto vehicle commands → CAN frames (unit conversion, topic-to-ID mapping, rate limiting). Dead reckoning odometry (speed + steer integration — drifts without absolute reference; full encoder+IMU fusion deferred to gap #5).

**What the Jetson controls:** Nothing directly. All actuation is via CAN commands to RT (which forwards to SYS/MTR and commands EPS-C). The Jetson is a pure perception/planning/command node — no GPIO, no analog I/O, no direct actuator wiring.

---

## 6. Design principles

1. **Queues over shared state.** No mutexes, no semaphores. Thread-safe queue pipes.
2. **ESTOP bypasses queues.** Safety task preempts and writes directly to actuators.
3. **One CAN ID = one sender per bus.** Every CAN ID has exactly one originator on a given bus. Each node's heartbeat uses its own ID (`0x7FD` RT, `0x7FE` SYS, `0x7FC` Jetson).
4. **Lower CAN ID = higher bus priority.** Safety IDs (`0x00X`) win arbitration.
5. **All multi-byte CAN fields are big-endian (MSB first)** — unless SYNTREE protocol specifies otherwise (see `0x169`, `0x7B9` in can-dictionary).
6. **Manual mode is pass-through, not dead.** SYS mirrors physical inputs to outputs.
7. **Actuators are standalone CAN modules.** EPS-C, SEB, and DC-DC are commanded via CAN.
8. **RT is the only dual-bus node.** No direct Jetson ↔ SYS path.
9. **Listen Before Speaking.** SYNTREE units require receiving status feedback before any command is sent. Boot state machines enforce this.
10. **EGAS 3-level safety separation for motor actuation.** The motor controller takes raw analog signals (0–5V throttle, 72V gear) with no internal intelligence. This is the only actuator without built-in CAN monitoring. Per ISO 26262, it is isolated on a dedicated STM32 (MTR) with three independent safety levels.
11. **Mode-gated dual control of SYNTREE actuators (Option D).** In AUTO, RT commands both EPS-C and SEB directly — 1-hop from the kinematics engine. In MANUAL, SYS commands SEB based on lever position; EPS-C runs standalone. The `0x7B9` SEB command is dual-sender but mode-gated — only one node transmits at a time, no collision. Per ISO 26262-5:2018 §7.4.4, this is redundant-controller practice. No single MCU failure can take both actuators offline.

---

### 6.1 EGAS 3-Level Motor Safety Architecture

The separation of **MTR (STM32)** from **SYS (ESP32-S3)** follows the EGAS 3-level electronic throttle monitoring concept (ISO 26262 ASIL-C):

```
Level 3: Hardware — ESTOP button wired direct to both MTR and SYS
         TPS3850 external watchdog on each MCU. No software, no CAN.
         ESTOP press → MTR cuts throttle + gear instantly (local).
         
Level 2: Function Monitor — SYS ESP32-S3
         Monitors MTR via CAN: compares 0x204 setpoint vs 0x206 feedback.
         Mismatch > threshold → CAN 0x001 ESTOP.
         Also handles QM body functions (lights, DCDC, indicators).
         
Level 1: Function Controller — MTR STM32
         Normal actuation: reads sensors, drives MCP4725 DAC + gear relays.
         MANUAL: pass-through from grip/gear. AUTO: follows CAN 0x204.
         No wireless, no OS, minimal attack surface.
```

| Principle | Implementation |
|-----------|---------------|
| **Freedom from interference** | Separate MCUs — a SYS crash/hang cannot block motor kill |
| **ASIL decomposition** | MTR (QM level sensor reads) + SYS monitor (ASIL-B comparison) → combined ASIL-C |
| **Independent safe state path** | ESTOP button wired to both MCUs — MTR cuts throttle locally, zero CAN delay |
| **Diverse monitoring** | SYS compares commanded speed vs actual from 0x206 — mismatch → ESTOP |
| **Why only motor needed MTR** | SYNTREE EPS-C and SEB already do EGAS Level 1 internally (CAN, PID, angle sensor, fault detection). Motor controller is a dumb analog device — it has no intelligence, no CAN, no internal monitoring. That gap is filled by the dedicated MTR board. |
| **Body control is QM** | Lights, indicators, DCDC, mode bulbs on SYS are non-safety — safe to share MCU with Level 2 monitoring per ISO 26262 QM classification |

### 6.1.1 MTR STM32 — I/O & Processing

The MTR STM32 is the EGAS Level 1 Function Controller — the only node with direct motor actuation authority. **Bare-metal, no OS, no wireless** — minimal attack surface per ISO 26262.

**What MTR does:** Reads throttle grip position (0–5V ADC) and gear selector position (72V via TLP281 optocouplers → 3.3V GPIO). In MANUAL mode, passes ADC straight through to MCP4725 DAC and mirrors gear selector to relay outputs. In AUTO mode, follows CAN `0x204` speed + gear commands. In ESTOP, hardware ISR instantly kills all outputs (Level 3). Reports motor speed and fault flags via CAN.

**CAN I/O with processing logic:**

| Input (RX) — DLC, fields | Processing | Output (TX) — DLC, fields | Controls |
|---------------------------|------------|---------------------------|----------|
| ESTOP GPIO (hardwired, NC, active-low) | Hardware ISR: DAC=0, all gear relays OFF, set `ESTOP_ACTIVE` (0x01) in fault_flags | `0x206` MTR_MOTOR_FBK — DLC=4, `{i16 actual_speed, u8 gear_state, u8 fault_flags}` with ESTOP_ACTIVE bit | Motor kill (Level 3) |
| Throttle ADC 0–5V, 12-bit (MANUAL mode) | `dac_value = adc_read()` — direct pass-through, dead zone 200 | MCP4725 DAC 0–5V, 12-bit → motor controller | Motor speed (MANUAL) |
| `0x204` RT_DRIVE_CMD — DLC=5, `{i32 speed_mmps, u8 gear}` (AUTO mode) | `dac_value = abs(speed)/3000 × 4095` — fixed voltage mapping (open-loop, no PID). Gear byte → energize matching relay (D/S/R); N → all off. | MCP4725 DAC 0–5V → motor controller; gear relay module → 72V ECU | Motor speed + gear (AUTO) |
| Gear sense GPIOs — TLP281 opto (MANUAL mode): D=12, S=13, R=14 | Mirror: D sense HIGH → energize D relay. Conflict (multiple HIGH) → treat as N (fail-safe). | Gear relay module → 72V ECU (D=33, S=34, R=35) | Motor gear (MANUAL) |
| `0x110` SYS_MODE_CMD — DLC=1, `{u8 mode (0=M, 1=A, 2=ESTOP)}` | Mode gate: AUTO → follow 0x204, MANUAL → ADC/gear pass-through, ESTOP → all zero | — (internal state) | Mode gating |
| `0x204` staleness >200ms (AUTO mode) | Zero speed + N gear (controlled stop, not ESTOP). Gated by 3s startup grace period. | MCP4725=0, all gear relays OFF | Stale command safety |
| `0x001` SAFETY_ESTOP — DLC=0 | Hardware ISR: immediate kill (redundant with hardwired ESTOP GPIO) | — | Emergency stop (CAN path) |
| `0x7FD` RT_HEARTBEAT — DLC=1, `{u8 alive_ctr}` | Monitor RT liveness (secondary; local ESTOP GPIO is primary kill path) | — (consumed locally) | RT liveness |
| Speed feedback (ADC or encoder) | Pack into i16 mm/s | `0x120` SYS_THROTTLE_STS — DLC=2, `{i16 speed_mmps}` → RT (fwd to Jetson), 100 Hz | Speed telemetry |
| State + faults | Pack gear_state, fault_flags (`ESTOP_ACTIVE`=0x01, etc.), actual_speed | `0x206` MTR_MOTOR_FBK — DLC=4, `{i16 actual_speed, u8 gear_state, u8 fault_flags}` → SYS, RT, 50 Hz | Motor feedback |

**Physical I/O:**

| Signal | Type | Range | Purpose |
|--------|------|-------|---------|
| Throttle ADC | Analog in, 12-bit | 0–5V | Grip position → speed demand |
| MCP4725 DAC | I²C out, addr 0x60, 12-bit | 0–5V | Drive motor controller throttle input |
| Gear D sense | TLP281 opto → GPIO | 72V → 3.3V | Gear selector D position (galvanic isolation) |
| Gear S sense | TLP281 opto → GPIO | 72V → 3.3V | Gear selector S position |
| Gear R sense | TLP281 opto → GPIO | 72V → 3.3V | Gear selector R position |
| Gear D relay | GPIO → relay → 72V | 72V fused, TVS protected | Energize D line to ECU |
| Gear S relay | GPIO → relay → 72V | 72V fused, TVS protected | Energize S line |
| Gear R relay | GPIO → relay → 72V | 72V fused, TVS protected | Energize R line |
| ESTOP button | GPIO, hardwired | NC, active-low, ISR | Hardware kill — zero CAN latency |

**What MTR manipulates:** Throttle voltage (maps ADC or CAN speed to 0–5V DAC output), gear selection (mirrors TLP281 inputs or follows CAN gear byte), fault flags (ESTOP_ACTIVE acknowledgment).

**What MTR controls:** Motor controller throttle via MCP4725 DAC (0–5V analog — the only non-CAN actuator in the system), motor gear via relay module (72V D/S/R lines to ECU). Speed control is open-loop; PID closure deferred until rear encoder fitted (gap #5).

### 6.1.2 Startup Grace Period — Hardware Safety Guarantees

A 3000ms startup grace period suppresses software safety checks (heartbeat
timeout, steering following-error, obstacle ESTOP) on RT and SYS during boot.
The vehicle relies on hardware defaults during this window:

| Component | Power-up State | Safety Guarantee |
|-----------|---------------|-----------------|
| MCP4725 DAC | Output = 0V (PD=1, 1kΩ pulldown per datasheet) | Motor controller sees 0V = zero throttle |
| Gear relays | GPIOs default to input/floating during MCU reset | Relay module requires active HIGH drive — floating = all relays OFF = neutral |
| EPS-C (steering) | Internal boot delay ~2s, then enters centering | Does not accept commands until internally aligned |
| SEB (brake) | Internal boot delay ~2s, then reports status | Does not actuate until commanded |
| TPS3850 watchdog | Enabled at power-on, 100ms window | Resets MCU if control loop fails during grace |
| ESTOP button | Hardwired NC to both SYS and MTR GPIOs | Works immediately — no software dependency |

**Hazard analysis:** If the vehicle is powered on while moving, no software
safety checks run for 3 seconds. The rider must hold the brake lever. Hardware
defaults (DAC=0V, relays=OFF) prevent unintended acceleration but do NOT
actively brake. The EPS-C centering sequence may briefly move the steering
wheel. These are acceptable for a prototype where the vehicle is always
stationary at power-on.

**Production hardening:** Add a "vehicle stationary" check before grace
expires — speed must be <50 mm/s from 0x120 for >500ms. If not stationary
after 3s, extend grace and log warning.

### 6.2 Mode-Gated Dual Control (Option D) — SYNTREE Actuators

The EPS-C and SEB are commanded by different nodes depending on mode. Four options were evaluated (see §6.3). Option D was selected:

**Why Option D over A (distributed fixed):** In AUTO, RT computes steering angle AND brake pressure from the same kinematics model. Option D sends both from RT — a single control tick produces both CAN frames. No cross-node sync needed for obstacle response. In A, brake required RT→0x205→SYS→0x7B9 (3 hops); in D it's RT→0x7B9 (1 hop).

**Why Option D over C (SYS owns both):** C makes SYS a single point of failure for both actuators. D preserves independent failure modes — RT failure loses AUTO steering/brake but SYS can still brake in MANUAL; SYS failure loses MANUAL brake but RT can still brake in AUTO. The EPS-C's internal timeout (20ms comm loss → lock) and SEB's internal timeout provide hardware backup regardless.

**Why not B (private CAN):** Adds hardware cost (MCP2515 + transceiver + bus wiring) and SYS bridging latency without safety improvement. The shared low-level CAN with SYNTREE's own rolling counter + checksum validation already prevents accidental actuation.

| Property | RT (AUTO) | SYS (MANUAL/ESTOP) |
|----------|-----------|---------------------|
| EPS-C (0x169) | Sends angle from kinematics | Does NOT send — EPS-C standalone |
| SEB (0x7B9) | Sends arbitrated brake kPa→pressure | Sends lever stroke / ESTOP max |
| 0x201 (EPS-C status) | Monitors angle for following error | Does not monitor |
| 0x721 (SEB status) | Does not monitor | Monitors stroke for boot sync |
| Mode switch | Receives 0x110 from SYS | Sends 0x110 on change |

> **Dual-sender exception**: `0x7B9` has two senders on the low-level bus. But only one is active at a time — mode-gated. In AUTO, RT sends continuously at 50 Hz and SYS is silent. In MANUAL/ESTOP, SYS sends and RT is silent. No simultaneous transmission in normal operation. The SEB accepts whichever frame has a valid checksum and rolling counter. This is standard automotive redundant-controller practice (ISO 26262-5:2018 §7.4.4).

### 6.3 Options Comparison (Archived)

| Criterion | A (distributed) | B (private CAN) | C (SYS only) | **D (mode-gated)** |
|-----------|----|-----|----|-----|
| Single point takes both actuators? | No | Yes | Yes | **No** |
| AUTO brake latency | ~30ms | ~40ms | ~20ms | **~20ms** |
| AUTO steer latency | ~20ms | ~30ms | ~30ms | **~20ms** |
| SYS failure → brake lost? | Yes | Yes | Yes | **No (RT takes over)** |
| RT failure → steer lost? | Yes | Same | No (SYS bridges) | Yes (EPS-C timeout backup) |
| Code complexity (1-5) | 3 | 4 | 4 | **3** |
| Hardware cost (1-5) | 4 | 2 | 4 | **4** |
| Safety score (1-5) | 4 | 2 | 2 | **5** |
| **Total (higher=better)** | 23 | 13 | 15 | **26** |

---

## 7. RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

### 7.1 Role

Converts ROS 2 motion commands (high CAN `0x300`) into:
- **Speed + gear** → low CAN `0x204` → SYS
- **Steering angle** → low CAN `0x169` → SYNTREE EPS-C

Bridges selected CAN messages (§2.3). Listens to `0x201 SES_STATUS` for steering feedback and safety monitoring.

**8 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

### 7.2 Dual CAN hardware

| Bus | Controller | Interface | GPIO | Transceiver |
|-----|-----------|-----------|------|-------------|
| Low-level | Built-in TWAI | Direct | TX=5, RX=4 | SN65HVD230 |
| High-level | MCP2515 | SPI | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SN65HVD230 |

### 7.3 CAN messages received

| Bus | ID | Name | Payload | Action |
|-----|-----|------|---------|--------|
| Low | `0x001` | SAFETY_ESTOP | — | `mode_set(Estop)`, forward to high |
| Low | `0x011` | SYS_SAFETY_STS | `{u8 estop, u8 hb_ok}` | Forward to high |
| Low | `0x110` | SYS_MODE_CMD | `u8 mode` | `mode_set(Manual/Auto)` |
| Low | `0x120` | SYS_THROTTLE_STS | `i16 speed_mmps` | Forward to high |
| Low | `0x201` | SES_STATUS | `{u8 status, i16 angle, u8 torq, …}` (8 bytes) | Steering feedback: sync boot angle, following error check |
| Low | `0x202` | SES_ErrInfo | 25 fault flags (8 bytes) | Log faults; escalate any L3 flag to ESTOP via `0x001` |
| Low | `0x203` | SES_Version | `{u8 sw_ver, u8 hw_ver}` | Log on boot for compatibility check |
| Low | `0x206` | MTR_MOTOR_FBK | `{i16 actual_speed, u8 gear_state, u8 fault_flags}` | Forward to high; monitor motor feedback |
| Low | `0x600` | SYS_DIAG_RPT | 8 bytes | Forward to high |
| Low | `0x6FA` | SES_Test | `{i16 mtr_curr, u16 ecu_temp, u16 pow_volt}` | Monitor motor current / ECU temp for degradation |
| Low | `0x7FE` | SYS_HEARTBEAT | `u8 alive_ctr` | Feed SYS alive counter (10 Hz); if no frame for >200ms → zero setpoints, RT takes over brake |
| High | `0x001` | SAFETY_ESTOP | — | `mode_set(Estop)`, forward to low |
| High | `0x300` | HOST_DRIVE_CMD | `{i32 speed, i32 yaw}` | → `cmd_queue` |
| High | `0x301` | HOST_BRAKE_REQ | `i32 pressure_kpa` | → atomic store |
| High | `0x302` | HOST_LIGHT_CMD | `u8` bitfield | Forward to low |
| High | `0x400` | HOST_OBSTACLE_DIST | `u32 distance_mm` | Obstacle distance for speed limiting (§7.6) |
| High | `0x7FC` | HOST_HEARTBEAT | `u8 alive_ctr` | Feed Jetson alive counter; frozen >1500ms → stale command |

### 7.4 CAN messages sent

| Bus | ID | Name | Payload | Rate |
|-----|-----|------|---------|------|
| Low | `0x001` | SAFETY_ESTOP | — | Event |
| Low | `0x204` | RT_DRIVE_CMD | `{i32 speed, u8 gear}` | 100 Hz |
| Low | `0x205` | RT_BRAKE_CMD | `i32 brake_pressure_kpa` | **50 Hz** |
| Low | `0x169` | VCU_SES_REQ | `{u8 ctrl, i16 angle, u8 speed, u8 sec, u8 cnt+cksum, u8 cksum}` (8 bytes) | **50 Hz** |
| Low | `0x302` | HOST_LIGHT_CMD (fwd) | `u8` bitfield | Change |
| Low | `0x7FD` | RT_HEARTBEAT | `u8 alive_ctr` | 2 Hz |
| High | `0x001` | SAFETY_ESTOP (fwd) | — | Event |
| High | `0x011` | SYS_SAFETY_STS (fwd) | `{u8 estop, u8 hb_ok}` | 5 Hz |
| High | `0x120` | SYS_THROTTLE_STS (fwd) | `i16 speed_mmps` | 100 Hz |
| High | `0x206` | MTR_MOTOR_FBK (fwd) | `{i16 actual_speed, u8 gear_state, u8 fault_flags}` | 50 Hz |
| High | `0x210` | RT_STATE_RPT | `{u8 mode, u8 steer_valid, u8 reversing}` | 10 Hz |
| High | `0x220` | RT_PID_RPT | RESERVED (inactive until encoders fitted) | — |
| High | `0x310` | STEER_DIAG | `{i16 angle, u8 fault, i16 motor_current, u16 ecu_temp, u8 reserved}` (8 bytes) | 10 Hz |
| High | `0x311` | BRAKE_DIAG | `{u16 pressure, u8 fault, i16 motor_current, u16 ecu_temp, u8 reserved}` (8 bytes) | 10 Hz |
| High | `0x600` | SYS_DIAG_RPT (fwd) | 8 bytes | 1 Hz |
| High | `0x7FD` | RT_HEARTBEAT | `u8 alive_ctr` | 2 Hz |

### 7.5 Internal data types

```cpp
enum class Mode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

enum class SteerState : uint8_t {
    STEER_BOOT_WAIT,          // 500ms power-on delay — do NOT transmit
    STEER_LISTEN_SYNC,        // Waiting for 0x201 SES_STATUS (angle + alignment), 5s timeout → FAULT
    STEER_ACTIVE,             // Normal operation — transmit 0x169 at 50 Hz
    ESTOP_RAMP_TO_ZERO,       // Non-obstacle ESTOP: ramp to 0° at 20°/s, continue transmitting
    ESTOP_HOLD_THEN_SILENT,   // Obstacle ESTOP: hold current angle 500ms, then silent-stop
    STEER_FAULT               // Timeout or silent-stop — stop transmitting
};

struct DriveCmd {
    int32_t speed_mmps      = 0;   // [-500, 3000]
    int32_t yaw_rate_mrad_s = 0;   // [-3000, 3000]
};

struct ResolvedSetpoint {
    int32_t motor_speed_mmps = 0;
    int32_t steer_angle_mdeg = 0;  // ±45000, +right (internal; convert to decideg for SYNTREE)
    uint8_t gear             = 0;
    bool    steer_valid      = false;
    bool    reversing        = false;
};
```

### 7.6 Control mechanisms

#### Tricycle kinematics

$$\delta = \arctan\left(\frac{L \cdot \omega}{|v|}\right) \quad L = 1500\text{ mm}$$

```
physics_resolve(cmd):
  1. Convert mm/s→m/s, mrad/s→rad/s
  2. If |v| > 50 mm/s: δ = atan2(L·ω, |v|), steer_valid = true
     Else: δ = steer_hold · 0.8 (decay), steer_valid = false
  3. Clamp δ to ±steer_limit (dynamic, see below)
  4. Clamp v to [-500, 3000] mm/s
  5. reversing = v < 0
  6. gear: v > 0 → D, v == 0 → N, v < 0 → R
```

#### Steering — SYNTREE EPS-C via CAN (`0x169`)

**Boot sequence — "Listen Before Speaking":**

```
State machine (steer_state_machine_loop):

STEER_BOOT_WAIT:
  - 500ms delay after power-on
  - DO NOT transmit any 0x169 frames
  - → STEER_LISTEN_SYNC

STEER_LISTEN_SYNC:
  - Wait for 0x201 SES_STATUS frame (timeout: 5s)
	  - On timeout → STEER_FAULT (rider can retry: short-press START → STEER_LISTEN_SYNC)
  - Extract SES_StrAngle (int16, scale 0.1°/bit → convert to internal mdeg)
  - CRITICAL: Set active_target_angle = current_physical_angle
  - Wait for SES_INF_Angle_Status == 1 (aligned)
  - → STEER_ACTIVE

STEER_ACTIVE:
  - Transmit 0x169 at 50 Hz
  - First frame commands wheels to stay exactly where they are
  - Then follow Jetson targets with dynamic clamp
  - Monitor following error: if |cmd - actual| > threshold for > timeout → ESTOP
	  - ESTOP behavior depends on trigger:
	      Obstacle-triggered: hold current angle (clamped to dynamic limit for speed),
	        silent-stop after 500ms. If current angle exceeds dynamic limit, ramp to
	        limit at 20°/s first, then hold. Prevents rollover during cornering + braking.
	      Non-obstacle (heartbeat loss, cmd stale, manual button): ramp to 0° at 20°/s,
	        continue transmitting 0x169 during ramp, then hold at 0°
	  - If following error persists during ESTOP centering ramp (>5° for >1s):
	      fall back to silent-stop (linkage likely mechanically jammed)

STEER_FAULT:
  - Stop transmitting 0x169
  - EPS-C will timeout-fault internally
	  - Short-press START button → reset to STEER_LISTEN_SYNC (manual retry)
	  - Long-press START (3s) + throttle at zero → force-activate with target=0°
	    (MANUAL mode only; AUTO bulb blinks 2 Hz = degraded steering, AUTO mode locked out)
```

**Unit conversion** (internal mdeg ↔ SYNTREE decideg):

```
SYNTREE raw = internal_angle_mdeg / 100   (45500 mdeg → 455 raw → 45.5°)
internal_mdeg = SYNTREE raw * 100         (455 raw → 45500 mdeg)
```

**SYNTREE protocol specifics:**

| Parameter | Value |
|-----------|-------|
| Command ID | `0x169` (factory default — SYNTREE preprogrammed, not reconfigurable. `0x7B9` is sent by RT in AUTO, SYS in MANUAL/ESTOP — one sender active at a time, no collision. `0x205` removed (RT now sends SEB directly)) |
| Rate | 50 Hz (20 ms period) — continuous transmission required |
| Control mode | 1 = Angle Mode |
| Angle range | ±700 raw (±70.0°, unit limit; software clamp tighter) |
| Angle resolution | 0.1°/bit (int16) |
| Slew rate | `VCU_SES_Tgt_StrAngleSpd` [°/s] — speed-dependent |
| Rolling counter | 4-bit, increment 0→15 every frame |
| Checksum | XOR of bytes 0–6, then `^ 0xFF` (verify against spec) |
| Security enables | Byte 5: `roll_cnt_enable=1`, `checksum_enable=1` |

**Safety mechanisms:**

| Mechanism | Description |
|-----------|-------------|
| **Software hard-stops** | Clamp commanded angle to ±40° (inside physical end-stops). Reject any Jetson command exceeding this regardless of unit's ±78° capability. |
| **Dynamic angle clamp** | Max allowable angle inversely proportional to `RT_PidMeasured` speed. At 25 km/h → max ~5°. At 2 km/h → max ~40°. Prevents rollover. |
| **Following error** | Compare commanded angle (`0x169`) vs actual (`SES_StrAngle` from `0x201`). If abs(error) > max(2°, 0.25×dynamic_limit) for > 300 ms → trigger ESTOP (stuck linkage / rock jam). (v0.0.5: was fixed 5°, now dynamic threshold.) |
| **Timeout fault** | If `0x169` stops for >20 ms, EPS-C triggers internal comm fault. RT must maintain 50 Hz in AUTO. |
| **Alignment check** | `SES_INF_Angle_Status` must be 1 before AUTO mode engages. Drive motor locked out until aligned. |

**Mode behavior:**

| Mode | Steering behavior |
|------|------------------|
| MANUAL | RT does NOT send `0x169`. EPS-C standalone. RT still listens `0x201` for telemetry. |
| AUTO | RT sends `0x169` at 50 Hz with resolved angle, dynamic clamp + slew rate applied. |
| ESTOP | Obstacle: hold current angle clamped to dynamic limit (ramp to limit if exceeding), silent-stop after 500ms. Non-obstacle: ramp to 0° at 20°/s via active `0x169`. Fall back to silent-stop if following error persists >1s. Both paths non-interruptible by START button (steering transition defers until ramp/hold complete). |

#### Speed control — open-loop today, PID-ready

Motor speed control is **open-loop**: `0x204` desired speed → MTR MCP4725 DAC = `speed/3000 × 4095` (fixed voltage mapping). No feedback compensation. Acceptable for flat-ground steady-state operation but will not compensate for hills, headwinds, or load changes.

**Why open-loop?** PID gains are defined in config.h (Kp=1.0, Ki=0.1, Kd=0.05) but the implementation is deferred. The rear motor encoder hasn't been physically fitted to the trike yet. The PCNT encoder inputs (GPIO1/2) currently read zero. Running PID against `measured=0` would command full throttle — it MUST NOT be active until the encoder is wired.

**Integration plan (gap #5):** Once the rear motor encoder is fitted:
1. `control_task` on RT feeds `speed_pid_update(desired, measured, dt)` with real encoder data
2. PID output (effort correction) is sent to SYS via an added field in `0x204` or a new CAN ID
3. MTR adds the PID trim to the base MCP4725 voltage — closing the loop
4. Until then, the PID runs as a **shadow controller** in RT, outputting to `0x220 RT_PID_RPT` (telemetry only) for validation against the open-loop command — the two should track closely on flat ground and diverge under load, telling us the PID gains are reasonable before wiring it in.

#### Obstacle speed limit

Jetson perception (LiDAR/camera/stereo) provides minimum obstacle distance via CAN `0x400` at 10 Hz. RT applies linear clamp: 300 mm→0, 3000 mm→full speed. Jetson's own MPPI planner produces safe speed targets; RT's limiter is a safety backstop, not the primary avoidance mechanism.

#### Command staleness watchdog

500 ms, checked @ 10 Hz. Zero `0x204` + stop `0x169`.

#### Brake arbitration (max-select)

```
brake_kpa = max(rt_computed_obstacle, jetson_request_0x301)
send 0x205 {brake_kpa} at 50 Hz on low bus → SYS brake_task
```

RT computes obstacle-emergency brake pressure from distance sensor. Jetson sends deceleration requests via `0x301`. The worse (higher) pressure wins. Result goes to SYS via `0x205 RT_BRAKE_CMD`.

SYS forwards to SEB in Pressure Mode when `0x205 > 0`, falling back to Stroke Mode for lever/ESTOP binary triggers.

### 7.7 RTOS task layout

```
Pri 5  can_rx_low   ── TWAI → can_rx_low_queue (16)
      can_rx_high  ── MCP2515 SPI → can_rx_high_queue (16)

Pri 4  dispatch     ◀── both RX queues
           Routes: high 0x300→cmd_queue, 0x301→atomic, 0x302→gw_tx_low
                   low 0x011→gw_tx_high, 0x120→gw_tx_high, 0x600→gw_tx_high
                   low 0x201→steer_feedback (sync angle, following error)
                   any 0x001→mode_set(Estop)+gateway, low 0x110→mode_set

Pri 4  control      ◀── cmd_queue (4, overwrite)
           100 Hz: kinematics  + dynamic angle clamp + obstacle + brake → setpoint_queue

Pri 3  can_tx_low   ◀── setpoint_queue + gw_tx_low_queue
           → 0x204 (100 Hz), 0x205 (50 Hz, drive setpoint), 0x169 (50 Hz, steer state machine), 0x302 (change)
      can_tx_high  ◀── telemetry + gw_tx_high_queue → 0x011,0x120,0x210,0x220,0x400,0x600

Pri 1  watchdog     ── 10 Hz staleness check
      heartbeat    ── 2 Hz 0x7FD on both buses
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx_low` | 5 | 4096 B | Event | `twai_receive()` → queue |
| `can_rx_high` | 5 | 4096 B | Event | MCP2515 SPI → queue |
| `dispatch` | 4 | 4096 B | Event | Route both RX queues + gateway + steer feedback |
| `control` | 4 | 4096 B | 100 Hz | Kinematics, dynamic angle clamp, obstacle, brake arbitration, gear derivation |
| `can_tx_low` | 3 | 3072 B | Event | 0x204@100Hz, 0x169@50Hz (steer SM gated), 0x302 |
| `can_tx_high` | 3 | 3072 B | Event | Telemetry → MCP2515 SPI |
| `watchdog` | 1 | 2048 B | 10 Hz | Staleness → zero setpoints + stop steer |
| `heartbeat` | 1 | 2048 B | 2 Hz | `0x7FD` on both buses (per-bus, not bridged): `alive_ctr++ & 0xFF`, DLC=1 |

### 7.8 Hardware pin assignments — Board: RT ESP32-S3

> ⚠️ **Board identity:** This is the **RT** ESP32-S3 (realtime physics, steering, CAN gateway). Do not confuse with SYS ESP32-S3 pin assignments in §8.8. Same GPIO numbers on different boards are different physical pins — connect to the board labeled "RT," not the one labeled "SYS."

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX (low) | 5 | Out | SN65HVD230 |
| CAN RX (low) | 4 | In | SN65HVD230 |
| SPI SCK | 36 | Out | MCP2515 (high CAN) |
| SPI MOSI | 37 | Out | MCP2515 |
| SPI MISO | 38 | In | MCP2515 |
| SPI CS | 39 | Out | MCP2515 |
| MCP INT | 40 | In | MCP2515 interrupt |
| Encoder A (rear motor) | 1 | In | Speed feedback (PCNT), quadrature |
| Encoder B (rear motor) | 2 | In | |
| Encoder A (front wheel) | 3 | In | Speed/angle feedback (PCNT), quadrature — **sensor TBD** |
| Encoder B (front wheel) | 6 | In | |
| Encoder A (rear left wheel) | 9 | In | Differential speed feedback (PCNT), quadrature — **sensor TBD** |
| Encoder B (rear left wheel) | 12 | In | |
| Encoder A (rear right wheel) | 13 | In | Differential speed feedback (PCNT), quadrature — **sensor TBD** |
| Encoder B (rear right wheel) | 14 | In | |
| I2C SDA | 10 | I/O | IMU (optional) |
| I2C SCL | 11 | Out | IMU (optional) |
| WDT toggle | 21 | Out | External watchdog IC (TPS3850). Toggled by `control_task` at 100 Hz. |

### 7.9 Configuration constants

```cpp
namespace rt {
// Vehicle
constexpr float kWheelbaseMM = 1500.0f;
// Steering (SYNTREE EPS-C)
constexpr float kSteerHardLimitDeg = 40.0f;       // software hard-stop inside mechanical limit
constexpr float kSteerFollowingErrMinDeg = 2.0f;   // floor threshold (was fixed 5.0)
constexpr float kSteerFollowingErrFactor = 0.25f;  // × dynamic_limit → threshold
constexpr int   kSteerFollowingErrMs = 300;        // duration before ESTOP
constexpr int   kSteerCmdRateHz = 50;              // SYNTREE requires 20ms period
constexpr int   kSteerBootWaitMs = 500;            // power-on delay before listening
// Dynamic angle clamp (0.0.4): limit_deg = 40.0 − (speed_kmh − 2.0) × (35.0/23.0), clamped [5.0, 40.0]
constexpr float kAngleClampBaseDeg    = 40.0f;   // max at 2 km/h
constexpr float kAngleClampMinDeg     =  5.0f;   // min at ≥25 km/h
constexpr float kAngleClampRangeDeg   = 35.0f;   // base − min
constexpr float kAngleClampSpeedRange = 23.0f;   // 25 − 2 km/h
// Speed limits
constexpr int kMaxSpeedFwdMmps = 3000, kMaxSpeedRevMmps = 500;
constexpr int kLowSpeedThreshMmps = 50;
// PID (deferred — see gap #5; gains TBD once encoders fitted)
// Obstacle
constexpr unsigned kObstacleStopMM = 300, kObstacleClearMM = 3000;
// Timing
constexpr int kControlLoopHz = 100, kCmdStaleTimeoutMs = 500;
constexpr int kHeartbeatIntervalMs = 500;
constexpr int kHeartbeatTimeoutMsSys = 200;       // SYS heartbeat loss (2 missed at 10 Hz). RT takes over 0x7B9.
constexpr int kHeartbeatTimeoutMsJetson = 1500;  // Jetson heartbeat loss (3 missed frames) → assisted stop
// CAN IDs are in shared/can/can_protocol.h (namespace can):
//   kIdRtHeartbeatLow (0x7FD), kIdSysHeartbeat (0x7FE), kIdJetsonHeartbeat (0x7FC),
//   kIdRtBrakeCmd (0x205), kIdSyntreeEpsCmd (0x169), kIdSyntreeSebCmd (0x7B9), etc.
constexpr int kWdtToggleGpio = 21;
// CAN
constexpr int kCanLowBitrateHz = 500000, kCanHighBitrateHz = 500000;
// Encoders (quadrature, PCNT)
constexpr int kEncRearMotorA = 1, kEncRearMotorB = 2;
constexpr int kEncFrontWheelA = 3, kEncFrontWheelB = 6;     // sensor TBD
constexpr int kEncRearLeftA = 9, kEncRearLeftB = 12;        // sensor TBD
constexpr int kEncRearRightA = 13, kEncRearRightB = 14;     // sensor TBD
} // namespace rt
```

### 7.10 Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| Low CAN bus-off | TWAI TEC > 255 | Log, auto-recover; ESTOP if persistent |
| High CAN bus-off | MCP2515 error flags | Log, auto-recover; zero setpoints until restored |
| Command stale | Watchdog 500 ms | Zero `0x204` + stop `0x169` |
| Obstacle timeout | Echo > 30 ms | Distance = UINT32_MAX |
| Rear motor encoder missing | Speed = 0 | open-loop - no encoder feedback (I-term saturates) |
| Wheel encoder missing (any) | No pulses for >1s at known speed | Log warning; differential odometry degraded. Does NOT trigger ESTOP. |
| Steering CAN TX fail | TWAI TX errors | Log, EPS-C will timeout-fault |
| Steering following error | abs(cmd − actual) > 5° for 300 ms | `mode_set(Estop)` |
| Steering sync timeout | No `0x201` within 5s of boot | → STEER_FAULT; rider can retry via START button short-press |
| Gateway queue full | `xQueueSend` fail | Drop (except 0x001 — direct TX) |

### 7.11 Startup

```
1. can_low_init() → TWAI, low CAN
2. can_high_init() → SPI + MCP2515, high CAN
3. steer SM → STEER_BOOT_WAIT (500ms) → STEER_LISTEN_SYNC (await 0x201) → STEER_ACTIVE
4. watchdog_init() → timestamp
5. heartbeat_init() → alive counter
6. Create queues (6), Create 8 tasks
7. ESP_LOGI("Ready")
```

### 7.12 I/O & Processing Summary

RT converts ROS 2 motion commands into motor speed + gear and steering angle commands. It is the only dual-bus node and bridges selected CAN messages. It monitors steering feedback and system liveness for safety.

**CAN I/O with processing logic:**

| Input (RX) — DLC, fields | Processing | Output (TX) — DLC, fields | Controls |
|---------------------------|------------|---------------------------|----------|
| `0x300` HOST_DRIVE_CMD — DLC=8, `{i32 speed_mmps, i24 yaw_rate_mrad_s, u8 gear}` | Tricycle kinematics: δ = atan2(L·ω, \|v\|), dynamic angle clamp, gear derivation (v>0→D, v=0→N, v<0→R) | `0x204` RT_DRIVE_CMD — DLC=5, `{i32 speed_mmps, u8 gear}` → MTR, SYS | Motor speed + gear |
| `0x301` HOST_BRAKE_REQ — DLC=4, `{i32 brake_pressure_kpa}` | Max-select arbitration: max(RT obstacle kPa, Jetson 0x301 kPa) | `0x205` RT_BRAKE_CMD — DLC=4, `{i32 brake_pressure_kpa}` → SYS; `0x7B9` VCU_SEB_REQ → SEB (AUTO, planned) | Brake pressure |
| `0x400` HOST_OBSTACLE_DIST — DLC=4, `{u32 distance_mm}` | Linear speed clamp: 0 at 300 mm → full at 3000 mm | Clamped speed in `0x204` | Obstacle speed limit |
| `0x201` SES_STATUS — DLC=8, `{u8 status, i16 angle, u8 torq, …}` | Steering SM: boot sync angle, alignment check, following-error monitor, SYNTREE rolling counter + XOR checksum | `0x169` VCU_SES_REQ — DLC=8, `{u8 ctrl, i16 angle, u8 speed, u8 sec, u8 cnt+cksum, u8 cksum}` → EPS-C (50 Hz, Angle Mode) | Steering angle |
| `0x110` SYS_MODE_CMD — DLC=1, `{u8 mode (0=M, 1=A, 2=ESTOP)}` | Mode gating: AUTO → transmit 0x169+0x204, MANUAL → silent, ESTOP → ramp/hold | Mode state gates TX | Mode control |
| `0x001` SAFETY_ESTOP — DLC=0 (no payload) | Immediate: ramp/hold steering, zero speed, max brake, forward to other bus | `0x001` DLC=0 to other bus + setpoints zeroed | Emergency stop |
| `0x7FE` SYS_HEARTBEAT — DLC=1, `{u8 alive_ctr}` | Liveness: 200ms timeout (2 missed at 10 Hz) → RT takes over `0x7B9` with stroke=max (full brake) | `0x7B9` VCU_SEB_REQ — DLC=8, `{u8 ctrl[2], u16 stroke, u16 press, u8 sec, u8 cksum}` (emergency takeover) → SEB | Brake takeover on SYS loss |
| `0x7FC` HOST_HEARTBEAT — DLC=1, `{u8 alive_ctr}` | Staleness: 1500ms timeout (3 missed at 2 Hz) → assisted stop (zero speed, stop steer, 2000 kPa brake, SYS→MANUAL) | `0x204`=0, `0x169` stop, `0x205`=2000 kPa | Assisted stop |
| `0x011` SYS_SAFETY_STS — DLC=3, `{u8 estop, u8 hb_ok, u4 light_state}` (low bus) | Transparent forward — same ID, same payload, forwarded to high bus | `0x011` on high bus → Jetson | CAN gateway |
| `0x120` SYS_THROTTLE_STS — DLC=2, `{i16 speed_mmps}` (low bus) | Transparent forward | `0x120` on high bus → Jetson | CAN gateway |
| `0x206` MTR_MOTOR_FBK — DLC=4, `{i16 actual_speed, u8 gear_state, u8 fault_flags}` (low bus) | Transparent forward | `0x206` on high bus → Jetson | CAN gateway |
| `0x600` SYS_DIAG_RPT — DLC=8, `{diag struct}` (low bus) | Transparent forward | `0x600` on high bus → Jetson | CAN gateway |
| `0x302` HOST_LIGHT_CMD — DLC=1, `{u8 lights bitfield}` (high bus) | Transparent forward | `0x302` on low bus → SYS | CAN gateway |
| `0x202` SES_ErrInfo — DLC=8, `{25 fault flags (8× L3)}` | Log faults; L3 flag → ESTOP via `0x001` | — (consumed locally) | Steering health |
| `0x203` SES_Version — DLC=8, `{u8 sw_ver, u8 hw_ver}` | Log on boot for compatibility check | — (consumed locally) | Steering health |
| `0x6FA` SES_Test — DLC=8, `{i16 mtr_curr, u16 ecu_temp, u16 pow_volt}` | Monitor motor current / ECU temp for degradation | — (consumed locally) | Steering health |
| — | Telemetry aggregation | `0x210` RT_STATE_RPT — DLC=3, `{u8 mode, u8 steer_valid, u8 reversing}` (10 Hz) | Host monitoring |
| — | Telemetry aggregation | `0x310` STEER_DIAG — DLC=8, `{i16 angle, u8 fault, i16 motor_current, u16 ecu_temp}` (10 Hz) | Host monitoring |
| — | Telemetry aggregation | `0x311` BRAKE_DIAG — DLC=8, `{u16 pressure, u8 fault, i16 motor_current, u16 ecu_temp}` (10 Hz) | Host monitoring |
| — | Heartbeat generation: alive_ctr++ per bus (NOT bridged) | `0x7FD` RT_HEARTBEAT — DLC=1, `{u8 alive_ctr}` on both buses (2 Hz) | Liveness |

**Physical I/O (RT board):**

| Signal | GPIO | Type | Purpose |
|--------|------|------|---------|
| CAN (low) | TX=5, RX=4 | TWAI | Low-level CAN bus |
| CAN (high) | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SPI → MCP2515 | High-level CAN bus |
| Encoder (rear motor) | A=1, B=2 | PCNT quadrature | Speed feedback (future PID) |
| Encoder (front wheel) | A=3, B=6 | PCNT quadrature | Sensor TBD |
| Encoder (rear wheels) | L:9/12, R:13/14 | PCNT quadrature | Differential speed (sensor TBD) |
| I²C | SDA=10, SCL=11 | I²C | IMU (optional) |
| WDT toggle | 21 | GPIO out, 100 Hz | TPS3850 external watchdog |

**What RT manipulates:** Speed setpoint (clamped by obstacle distance), steering angle (resolved from kinematics + dynamic clamp), brake pressure (arbitrated max-select), CAN frames (bridged between buses), mode state (gates actuator transmission).

**What RT controls:** EPS-C steering angle via `0x169` (Angle Mode, 50 Hz, SYNTREE rolling counter + checksum security), MTR motor speed + gear via `0x204` (100 Hz), SEB brake via `0x205`→SYS→`0x7B9` (50 Hz, Pressure/Stroke Mode). CAN gateway message forwarding between low and high buses.

---

## 8. SYS ESP32-S3 — Safety, Motor Actuation & Body Control

### 8.1 Role

Safety (E-stop, brake lever, RT heartbeat), motor actuation (0–5V via MCP4725, 72V gear via relays), brake control (SYNTREE SEB via CAN `0x7B9`), DC-DC converter, signal lights, mode indicators, 12V power, diagnostics.

Low-level CAN only. Jetson communication via RT.

**15 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

### 8.2 CAN interface

Built-in TWAI, GPIO 4/5, 500 kbit/s, SN65HVD230.

### 8.3 CAN messages received

| ID | Name | Payload | Source | Action |
|----|------|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | — | RT or any | `mode_set(Estop)` |
| `0x204` | RT_DRIVE_CMD | `{i32 speed, u8 gear}` | RT | → `setpoint_queue`; stale >200ms → zero speed + N |
| `0x205` | RT_BRAKE_CMD | `i32 brake_pressure_kpa` | RT | → `g_brake_pressure_kpa` atomic; >0 → SEB Pressure Mode |
| `0x206` | MTR_MOTOR_FBK | `{i16 actual_speed, u8 gear_state, u8 fault_flags}` | MTR | EGAS L2: compare speed setpoint vs actual; mismatch → ESTOP |
| `0x302` | HOST_LIGHT_CMD (fwd) | `u8` bitfield | RT | → `g_light_state` |
| `0x6FB` | SEB_Test | `{i16 mtr_curr, u16 ecu_temp, u16 pow_volt}` | SEB | Monitor motor current / ECU temp trends for degradation early warning |
| `0x721` | SEB_STATUS | `{u8 status, u16 stroke, u16 angle, u8 press, …}` (8 bytes) | SEB | Sync boot stroke, brake feedback, error level |
| `0x731` | SEB_ErrInfo | 23 fault flags (8 bytes) | SEB | Log faults; escalate any L3 flag to ESTOP via `0x001` |
| `0x741` | SEB_Version | `{u8 sw_ver, u8 hw_ver}` | SEB | Log on boot for compatibility check |
| `0x7FD` | RT_HEARTBEAT | `u8 alive_ctr` | RT | Feed RT alive counter; timeout >1000ms → ESTOP. Faster detection: `0x204` staleness at 200ms → zero speed. |

### 8.4 CAN messages sent

| ID | Name | Payload | Rate | Notes |
|----|------|---------|------|-------|
| `0x001` | SAFETY_ESTOP | — | Event | → All nodes (ESTOP on L3 fault, RT heartbeat loss) |
| `0x011` | SYS_SAFETY_STS | `{u8 estop, u8 hb_ok, u4 light_state}` | 5 Hz | → RT (fwd to Jetson) |
| `0x012` | SYS_DCDC_CMD | `u8 enable` | Change | → DC-DC converter |
| `0x110` | SYS_MODE_CMD | `u8 mode` | Change | → RT |
| `0x600` | SYS_DIAG_RPT | 8 bytes | 1 Hz | → RT (fwd to Jetson) |
| `0x7B9` | VCU_SEB_REQ | `{u8 ctrl[2], u16 stroke, u16 press, u8 sec, u8 cksum}` (8 bytes) | **50 Hz** | → SYNTREE SEB |
| `0x7FE` | SYS_HEARTBEAT | `u8 alive_ctr` | 10 Hz | → RT |

### 8.5 Internal data types

```cpp
enum class SysMode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

enum class BrakeState : uint8_t {
    BRAKE_BOOT_WAIT,     // 500ms — do NOT transmit
    BRAKE_LISTEN_SYNC,   // Wait for 0x721 SEB_STATUS, read current stroke
    BRAKE_ACTIVE,        // Transmit 0x7B9 at 50 Hz, synced to SEB
    BRAKE_DEGRADED       // LISTEN_SYNC timeout — transmit with lever defaults, no sync
};

struct ActuatorSetpoint {
    int32_t motor_speed_mmps = 0;
    Gear    gear             = Gear::N;
};
```

### 8.6 Control mechanisms

#### Throttle — MCP4725 I2C DAC (0–5V)

| Parameter | Value |
|-----------|-------|
| DAC device | MCP4725, I2C addr 0x60, SDA=GPIO15, SCL=GPIO16 |
| Resolution | 12-bit (0–4095), VCC=5V → 0–5V output (no op-amp) |
| ADC read | ADC1_CH5, GPIO10, 12-bit, voltage divider 5V→3.3V |
| Dead zone | 200 (raw ADC) |
| Max speed | 3000 mm/s |
| Update rate | 100 Hz |

| Mode | Behavior |
|------|----------|
| MANUAL | ADC read → MCP4725 write (pass-through) |
| AUTO | `setpoint.speed` → `abs(speed)/3000 × 4095` → MCP4725 |
| ESTOP | MCP4725 = 0 |

Direction via gear lines — MCP4725 outputs 0–5V proportional to speed magnitude only.

#### Gear — TLP281 input + relay output (72V)

**Input** (manual sense, galvanic isolation):

| Signal | GPIO | Conditioning |
|--------|------|-------------|
| D sense | 12 | TLP281 optoisolator ch1 (72V→3.3V) |
| S sense | 13 | TLP281 optoisolator ch2 |
| R sense | 14 | TLP281 optoisolator ch3 |

**Output** (mimic 72V to ECU):

| Signal | GPIO | Path |
|--------|------|------|
| D out | 33 | Relay ch1: GPIO→IN, 72V→1A fuse→COM→NO→ECU, TVS SMCJ90CA→GND |
| S out | 34 | Relay ch2: same path |
| R out | 35 | Relay ch3: same path |

| Mode | Behavior |
|------|----------|
| MANUAL | Read TLP281 → mirror to relays |
| AUTO | Gear from CAN `0x204` → energize relay |
| ESTOP | All OFF (N) |

#### DC-DC converter — CAN `0x012`

| Condition | CAN `0x012` | 12V accessory relay (GPIO27) |
|-----------|------------|------------------------------|
| MANUAL or AUTO | `enable = 1` | ON (all accessories powered) |
| ESTOP | **`enable = 1`** (maintains 12V for MCUs, CAN transceivers, and brake light) | OFF (cuts headlight, turn signals, mode bulbs) |

Sent on state change. **DC-DC stays ON during ESTOP** — MCUs need 12V→3.3V to run, CAN transceivers need 5V, and the brake light must illuminate during ESTOP (fail-visible). The 12V accessory relay (GPIO27) provides the secondary cut for non-safety loads. The brake light is wired to the always-on DC-DC output, not through the accessory relay, so it remains powered during ESTOP.

#### Signal lights

| Signal | GPIO | Notes |
|--------|------|-------|
| Left turn | 18 | Blink 500ms on/off while active |
| Right turn | 19 | Blink 500ms on/off while active |
| Brake light | 21 | **OR of all braking sources** (see below) |
| Headlight | 22 | On/off |

**Brake light logic — OR of all braking sources:**

```
brake_light_on = safety_brake_lever_pressed()   // GPIO2 — physical lever
              OR (mode == Estop)                // ESTOP — full brake
              OR g_light_state.brake_light;     // Jetson CAN 0x302 — predictive / hazard
              // Future: OR (brake_stroke_mm > 0) — SEB feedback confirms actual braking
```

All four sources are local to SYS. `g_light_state.brake_light` from Jetson is a **supplemental** trigger — useful for predictive illumination (Jetson sees obstacle before pressure builds) or hazard flashing — but can never be the *only* trigger. The physical braking state always wins.

**Manual mode — handlebar switches:**

| Switch | GPIO | Type | Action |
|--------|------|------|--------|
| Left turn | 3 | Momentary, active-low, pull-up | Press → left turn blinks (500ms on/off). Press again → cancel. |
| Right turn | 6 | Momentary, active-low, pull-up | Press → right turn blinks. Press again → cancel. |
| Headlight | 7 | Toggle, active-low, pull-up | Each press toggles headlight on/off. |

**Mode-dependent behavior:**

| Mode | Turn signals | Headlight | Brake light |
|------|-------------|-----------|-------------|
| MANUAL | Handlebar switches → `lights_task` | Handlebar switch → GPIO22 | **OR logic** — lever + Jetson bit |
| AUTO | `g_light_state` from CAN `0x302` | `g_light_state.headlight` | **OR logic** — lever + ESTOP + Jetson bit |
| ESTOP | OFF | OFF | **ON** (forced, overrides all) |

**Turn signal blink pattern** (`lights_task`):
- 500 ms ON, 500 ms OFF, repeating while `left_turn` or `right_turn` is active
- Pressing the same switch again cancels (toggle behavior)
- Pressing the opposite switch cancels the current side and starts the new side
- Both pressed simultaneously → hazard flashers (both blink in sync)

**Headlight toggle** (`lights_task`):
- Each press of GPIO7 toggles `headlight_on = !headlight_on`
- AUTO mode: override from `g_light_state.headlight` via CAN `0x302`

#### Mode switch — push button toggle

A momentary push button on GPIO11 (active-low, internal pull-up, debounced). Each press toggles the mode: MANUAL → AUTO → MANUAL. ESTOP cannot be exited via the MODE button — use the START button (GPIO32) or power-cycle.

```
mode_task @ 10 Hz:
  // Mode toggle button (GPIO11)
  read GPIO11
  if falling edge (prev_mode==HIGH, now==LOW) and debounce == 0:
      if current mode == MANUAL → mode_set(Auto)
      elif current mode == AUTO  → mode_set(Manual)
      debounce = kDebounceMs / 100

  // Start button (GPIO32) — exit ESTOP
  read GPIO32
  if falling edge (prev_start==HIGH, now==LOW) and debounce == 0:
      if current mode == ESTOP → mode_set(Manual)
      debounce = kDebounceMs / 100

  if debounce > 0: debounce--
  prev_mode = GPIO11; prev_start = GPIO32
```

> A toggle switch would require the rider to physically change switch position. A push button is simpler to operate while riding — one tap to switch modes.

#### Mode indicator bulbs

Two relay-driven bulbs (visible in sunlight, not just PCB LEDs):

| Signal | GPIO | Active for |
|--------|------|-----------|
| AUTO bulb | 25 | AUTO mode |
| MANUAL bulb | 26 | MANUAL mode |
| (both OFF) | — | ESTOP |

> GPIO → relay coil → bulb. Bulbs are powered from the 12V accessory rail — they go dark on ESTOP regardless of MCU state.

#### 12V relay — unchanged

GPIO27 (HIGH=ON, ESTOP→OFF).

#### Heartbeat — automotive liveness supervision

Each node sends a **1-byte alive counter** on its own CAN ID at 2 Hz. A frozen counter = a frozen node, even if the CAN controller is still DMA-ing from a hardware buffer.

**Heartbeat IDs (one sender per ID):**

| ID | Sender | Bus | Receiver |
|----|--------|-----|----------|
| `0x7FD` | RT | Both (per-bus, NOT bridged) | SYS (low), Jetson (high) |
| `0x7FE` | SYS | Low only | RT |
| `0x7FC` | Jetson | High only | RT |

**Why heartbeats are NOT bridged:**

Each CAN bus is an independent liveness domain. RT sends `0x7FD` independently on each bus (same ID, separate alive counters per bus). SYS and Jetson heartbeats never leave their respective buses. This means every receiver sees exactly one sender per heartbeat ID — no ambiguity, no software demuxing needed.

**Liveness matrix (who monitors whom):**

```
       SYS ──── low CAN ──── RT ──── high CAN ──── Jetson
              0x7FE (SYS)          0x7FC (Jetson)
              0x7FD (RT)           0x7FD (RT)

SYS watches:   RT (0x7FD on low, 1000ms timeout)
RT watches:    SYS (0x7FE on low, 200ms timeout — 2 missed frames at 10 Hz) AND Jetson (0x7FC on high, 1500ms timeout)
Jetson watches: RT (0x7FD on high, 1500ms timeout)

SYS does NOT watch Jetson — RT handles Jetson failure (zero setpoints)
Jetson does NOT watch SYS — RT handles SYS failure (0x001 ESTOP)
```

| Parameter | RT↔SYS (low bus) | RT→Jetson (high) | Jetson→RT (high) |
|-----------|------------------|-------------------|-------------------|
| RT heartbeat ID | `0x7FD` | `0x7FD` | — |
| Peer heartbeat ID | `0x7FE` (SYS) | — | `0x7FC` (Jetson) |
| DLC | 1 | 1 | 1 |
| Payload | `u8 alive_ctr` (0–255, wraps) | `u8 alive_ctr` | `u8 alive_ctr` |
| Period | RT: 500 ms (2 Hz) / SYS: 100 ms (10 Hz) | 500 ms (2 Hz) | 500 ms (2 Hz) |
| Timeout | **200ms** (2 missed frames) | 1500ms (3 missed frames) | **1500ms** (3 missed frames) |
| Action on loss | **RT takes over 0x7B9 (stroke=max) + CAN 0x001.** MTR kills motor/gear locally. Brake gap ≤220ms. | Jetson: **assisted stop** — zero 0x204 + stop 0x169 + 0x205 brake=2000 kPa + SYS→MANUAL. Rider can override. | RT: zero `0x204` + stop `0x169` |

**Why 200ms for SYS heartbeat (10 Hz):**

SYS controls the SEB brake actuator — there is no independent fast-path check for brake command loss (unlike steering which has the 300ms following-error check, and motor which has the 200ms 0x204 staleness check). The heartbeat IS the brake fast-path check. At 25 km/h, 200ms is ~1.4m of travel — within FTTI for brake loss. At 10 Hz (100ms period), 2 missed frames = 200ms worst case. This is tight enough for brake safety while providing a 100ms debounce against CAN jitter.

**RT brake takeover on SYS heartbeat loss:** Architecture §6.2 Option D explicitly requires RT to take over brake on SYS failure: "SYS failure → brake lost? No (RT takes over)." When RT detects SYS heartbeat loss, RT immediately begins transmitting 0x7B9 with stroke=max (full brake) at 50 Hz, regardless of current mode. RT already knows the full SYNTREE SEB protocol — this is the same transmission it performs in AUTO mode, with stroke=max substituted for the computed pressure target. The mode gate opens on emergency: both RT and SYS may briefly transmit 0x7B9 during the takeover transition, which is within the dual-sender exception documented in §6.2. Total brake gap: 200ms detection + 20ms first frame = 220ms worst case.

**Jetson→RT at 1500ms — assisted stop:**

Jetson runs the perception stack (obstacle detection). When it dies, the vehicle must decelerate actively — pure coast is insufficient in traffic. Three missed frames at 2 Hz = 1500ms protects against false triggers from Jetson's non-realtime Linux CAN stack (a Linux machine that can't send a single CAN frame in 1.5s is genuinely dead). On timeout, RT commands: zero 0x204 speed, stop 0x169 steering, 0x205 brake = 2000 kPa (~2 MPa, moderate deceleration without wheel lockup), and transitions SYS to MANUAL mode. Brake light illuminates. Rider can override with lever. This is "assisted stop" — between pure coast and full ESTOP — providing active deceleration while preserving lights and rider agency.

**MTR-side `0x204` staleness check (fast path):**

MTR's control loop checks data freshness independently of heartbeat. If `0x204 RT_DRIVE_CMD` stops arriving in AUTO mode (RT control loop crashed, CAN TX failed):

```
motor_task @ 100Hz:
  if (time_since_last_0x204 > 200ms):  // 2 missed frames at 100 Hz
      setpoint.speed = 0
      setpoint.gear = N
```

This is a data-quality check, not a node-liveness check. It triggers in 200ms (vs 1000ms for heartbeat) because stale data is immediately dangerous. Combined with the heartbeat, SYS now has two independent checks:

| Check | Detects | Timeout | Action |
|-------|---------|---------|--------|
| `0x204` staleness | RT control loop stopped | 200ms | Zero speed + neutral |
| RT heartbeat loss (`0x7FD`) | RT node dead | 1000ms | ESTOP (full stop) |

**Startup grace period:**

At boot, heartbeats haven't been established. `safety_heartbeat_ok()` returns `true` for the first **3 seconds** if `last_hb_timestamp == 0`. After the grace period, real heartbeat checking begins. This prevents false ESTOP during boot.

```cpp
bool safety_heartbeat_ok() {
    int64_t now = esp_timer_get_time();
    if (last_hb_rt_us == 0) {
        return (now < kStartupGracePeriodUs);  // 3_000_000
    }
    return ((now - last_hb_rt_us) / 1000) < kHeartbeatTimeoutMs;  // 1000
}
```

**Alive counter validation:**

A heartbeat frame with the same counter value as the previous frame = stuck CAN controller (MCU hung, controller still DMA-ing from buffer). Treated as a missed heartbeat.

```cpp
bool heartbeat_is_fresh(uint8_t new_ctr) {
    if (new_ctr != last_alive_ctr) { last_alive_ctr = new_ctr; return true; }
    return false;  // frozen — same counter twice
}
```

#### External watchdog

Each ESP32 toggles a dedicated **external watchdog GPIO** every iteration of its highest-priority task. A hardware window watchdog IC (e.g., TPS3850) resets the MCU if toggling stops for >100 ms. On reset, all outputs default to safe state.

| Node | GPIO | Toggled by | Period | Watchdog IC |
|------|------|-----------|--------|-------------|
| RT | **21** | `control_task` | 100 Hz | TPS3850 or equiv, 100ms window |
| SYS | **23** | `safety_task` | 20 Hz | TPS3850 or equiv, 100ms window |

> This is independent of CAN heartbeat. A hung MCU with a frozen CAN controller is invisible to heartbeat — but the external watchdog catches it.

#### Physical controls

Three buttons on the dashboard:

| Button | GPIO | Type | Action |
|--------|------|------|--------|
| **ESTOP** | 1 | Big red mushroom, NC, active-low, hardware ISR | Instant ESTOP — motor kill, brake engage, DCDC off |
| **START** | 32 | Green momentary, active-low, pull-up, debounced | Exit ESTOP → MANUAL. No effect in AUTO/MANUAL. |
| **MODE** | 11 | Momentary, active-low, pull-up, debounced | Toggle MANUAL ↔ AUTO. Ignored in ESTOP. |

Plus brake lever on GPIO2 (active-low, pull-up). Safety task polls ESTOP + brake lever at 20 Hz. Mode task handles MODE + START buttons at 10 Hz.

> Industrial safety pattern: separate STOP (red mushroom) and START (green) buttons. STOP is NC (normally-closed) — a cut wire triggers ESTOP, not a failure-silent state.

#### Brake — SYNTREE SEB via CAN (`0x7B9`)

**Boot sequence — "Listen Before Speaking":**

```
BRAKE_BOOT_WAIT:
  - 500ms delay after power-on
  - DO NOT transmit any 0x7B9 frames
  - → BRAKE_LISTEN_SYNC

BRAKE_LISTEN_SYNC:
  - Wait for 0x721 SEB_STATUS frame
  - Extract SEB_Stroke_Value (u16, scale 0.05, offset -30)
  - Set initial command target = current stroke (hold position)
  - Wait for SEB_Alignment_Status == 1
  - → BRAKE_ACTIVE

BRAKE_ACTIVE:
  - Transmit 0x7B9 at 50 Hz continuously
  - Rolling counter increments 0→15 every frame
  - Checksum = XOR(bytes 0–6) ^ 0xFF (verify against spec)

BRAKE_DEGRADED:
  - Entered when LISTEN_SYNC times out (no 0x721 within 2s)
  - Transmit 0x7B9 at 50 Hz with conservative defaults:
      Lever pressed (GPIO2 LOW) → stroke = kBrakeManualStroke (~15 mm)
      Lever released → stroke = 0 mm
  - Rolling counter still increments, checksum still computed
  - When first valid 0x721 arrives: sync current stroke, → BRAKE_ACTIVE
  - Diagnostic flag set in 0x600 SYS_DIAG_RPT
  - Brake lever remains functional — this is not a terminal fault state
```

**SYNTREE SEB protocol:**

| Parameter | Value |
|-----------|-------|
| Command ID | `0x7B9` |
| Rate | 50 Hz (20 ms) — continuous transmission required |
| Control mode | 0 = Stroke Mode, 1 = Pressure Mode (1-bit per CSV) |
| Stroke range | -5 to 27 mm (raw: 500–1140, scale 0.05, offset -30) |
| Pressure range | 0 to 5 MPa (raw: 0–100, u8, scale 0.05 MPa/bit, offset 0) |
| Pressure conversion | `seb_raw = kPa × 0.02` (1 MPa = 1000 kPa; 1 bit = 0.05 MPa) |
| Rolling counter | 4-bit, increment every frame |
| Checksum | XOR of bytes 0–6, then `^ 0xFF` (verify against spec) |

> **Pressure Mode (1) — verified kPa→raw conversion:**
> 
> RT sends `0x205 {i32 brake_pressure_kpa}`. SYS converts using verified SYNTREE SEB spec:
> `seb_raw = (uint8_t)(kpa * 0.02f)`. Scale: 0.05 MPa/bit, range 0–5 MPa (raw 0–100). At 5000 kPa (5 MPa) → raw=100. Clamp to `kSebMaxPressureRaw` (100).
> 
> **Mode-switching:** 0x205 transitions 0→positive: hold current stroke, switch mode bit to 1 (Pressure), ramp pressure from current measured to target. 0x205 drops to 0: switch mode to 1 (Stroke), stroke=0. Prevents pressure transients.

**Mode-dependent behavior:**

| Mode | Brake behavior |
|------|---------------|
| MANUAL | Brake lever GPIO2 LOW → stroke = `kBrakeManualStroke` (~15 mm). Released → stroke = 0 mm. Transmit at 50 Hz in Stroke Mode. |
| AUTO | Lever pressed → `kBrakeManualStroke` (driver override always wins). No lever + `0x205 > 0` → Pressure Mode with kPa→MPa mapping. No lever + `0x205 == 0` → stroke = 0. ESTOP → max. |
| ESTOP | Stroke = `kBrakeMaxStroke` (full brake, ~27 mm). Transmit at 50 Hz in Stroke Mode. |

**Stroke value calculation:**

```
Physical stroke [mm] → raw = (physical + 30.0) / 0.05
Example: 0 mm → (0+30)/0.05 = 600
         15 mm → (15+30)/0.05 = 900
         27 mm → (27+30)/0.05 = 1140
```

### 8.7 RTOS task layout

```
Pri 5  can_rx      ── TWAI → can_rx_queue (16)
       safety      ── GPIO poll @ 20 Hz → ESTOP / HB check

Pri 4  dispatch    ◀── can_rx_queue: 0x204→setpoint, 0x302→light, 0x001→ESTOP, 0x721→brake_feedback
       mode        ── Push button (GPIO11) @ 10 Hz → toggle MANUAL↔AUTO, CAN 0x110
       motor       ◀── setpoint_queue (4, overwrite)
             100 Hz: AUTO→MCP4725+gear, MANUAL→pass-through, ESTOP→all off

Pri 3  throttle    ── ADC @ 100 Hz → CAN 0x120
       gear        ── Gear FSM @ 50 Hz
       brake       ── Brake FSM @ 50 Hz → CAN 0x7B9 (continuous, rolling ctr + cksum)
       lights      ── Light FSM @ 20 Hz (blink timing, ESTOP=brake ON)
       dcdc        ── DCDC FSM @ 5 Hz → CAN 0x012

Pri 2  indicator   ── Mode bulbs @ 5 Hz
       power       ── 12V relay @ 5 Hz
       can_tx      ── Safety status @ 5 Hz → CAN 0x011

Pri 1  diag        ── System health @ 1 Hz → CAN 0x600
       hb          ── 0x7FE @ 10 Hz
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx` | 5 | 4096 B | Event | `twai_receive()`, copy to queue |
| `safety` | 5 | 2048 B | 20 Hz | ESTOP GPIO, RT HB timeout, EGAS L2: compare 0x204 vs 0x206 |
| `dispatch` | 4 | 3072 B | Event | Route 0x206, 0x302, 0x001, 0x721 |
| `mode` | 4 | 2048 B | 10 Hz | MODE btn toggle + START btn (ESTOP→MANUAL), CAN 0x110 → RT + MTR |
| `brake` | 3 | 2048 B | **50 Hz** | Brake SM (boot sync) + CAN 0x7B9 with rolling ctr + checksum |
| `lights` | 3 | 1536 B | 20 Hz | Light GPIOs + blink |
| `dcdc` | 3 | 1024 B | 5 Hz | DCDC FSM, CAN 0x012 |
| `indicator` | 2 | 1024 B | 5 Hz | Mode bulbs (AUTO/MANUAL) |
| `power` | 2 | 1024 B | 5 Hz | 12V relay |
| `can_tx` | 2 | 3072 B | 5 Hz | CAN `0x011` (mode task sends `0x110`) |
| `diag` | 1 | 2048 B | 1 Hz | CAN 0x600 |
| `hb` | 1 | 2048 B | 10 Hz | CAN `0x7FE` (SYS heartbeat to RT, fast path for brake loss detection) |

> **Note:** Motor actuation (MCP4725 DAC), throttle ADC, and gear relay control run on SYS in MANUAL/AUTO modes. MTR STM32 handles only the bare-metal motor controller interface (EGAS L1). In ESTOP, SYS zeros the DAC and disengages all relays.

### 8.8 Hardware pin assignments — Board: SYS ESP32-S3

> ⚠️ **Board identity:** This is the **SYS** ESP32-S3 (safety, brake control, body control). Motor actuation is on the dedicated **MTR** STM32 board (§5.0). Do not confuse with RT ESP32-S3 pin assignments in §7.8. Same GPIO numbers on different boards are different physical pins — connect to the board labeled "SYS," not the one labeled "RT."
>
> **Shared GPIO numbers (safe — different chips):** ESTOP button is shared between SYS GPIO1 and MTR (hardwired to both MCUs for Level 3 kill). No other signals are split. GPIO 3,6,7 appear in both RT and SYS tables but are physically separate pins on separate ESP32-S3 packages — no electrical conflict. Throttle ADC, gear sense, MCP4725 I2C, and gear outputs are on MTR only (§5.0).

| Signal | GPIO | Direction | Conditioning |
|--------|------|-----------|-------------|
| CAN TX (low) | 5 | Out | SN65HVD230 |
| CAN RX (low) | 4 | In | SN65HVD230 |
| E-stop button | 1 | In | Big red mushroom, NC (active-low), pull-up, hardware ISR. Shared with MTR. |
| Brake lever | 2 | In | Active-low, pull-up → CAN 0x7B9 → SEB |
| Start button | **32** | In | Momentary, active-low, pull-up, debounced. Exits ESTOP → MANUAL. |
| Mode button | 11 | In | Push button, active-low, pull-up, debounced (momentary toggle MANUAL↔AUTO). Publishes CAN 0x110 to RT + MTR. |
| Left turn switch | **3** | In | Handlebar switch, active-low, pull-up |
| Right turn switch | **6** | In | Handlebar switch, active-low, pull-up |
| Headlight switch | **7** | In | Handlebar toggle, active-low, pull-up |
| Left turn | 18 | Out | Relay → lamp |
| Right turn | 19 | Out | Relay → lamp |
| Brake light | 21 | Out | Relay → lamp |
| Headlight | 22 | Out | Relay → lamp |
| AUTO bulb | 25 | Out | Relay → bulb (12V rail) |
| MANUAL bulb | 26 | Out | Relay → bulb (12V rail) |
| 12V relay | 27 | Out | Secondary cut on ESTOP |
| WDT toggle | **23** | Out | External watchdog IC (TPS3850). Toggled by `safety_task` every 50 ms. |

> **Motor I/O (throttle, gear, MCP4725) currently runs on SYS ESP32-S3. Migration to MTR STM32 is tracked as architecture gap #5. See §5 responsibility table footnote and docs/mtr-migration.md.**

### 8.9 Configuration constants

```cpp
namespace sys {
// CAN
constexpr int kCanBitrateHz = 500000, kCanTxGpio = 5, kCanRxGpio = 4;
// Safety
constexpr int kEstopGpio = 1, kBrakeLeverGpio = 2, kModeSwitchGpio = 11;
constexpr int kWdtToggleGpio = 23;
// Throttle/gear I/O is on MTR (mtr-stm32/src/config.h), not SYS
// Light inputs (handlebar switches, MANUAL mode)
constexpr int kSwitchLeftTurn = 3, kSwitchRightTurn = 6, kSwitchHeadlight = 7;
// Light outputs
constexpr int kLightLeftTurn = 18, kLightRightTurn = 19;
constexpr int kLightBrake = 21, kLightHead = 22;
// Indicators & power
constexpr int kBulbAuto = 25, kBulbManual = 26, kPower12vRelay = 27;
constexpr int kModeBtnGpio = 11, kStartBtnGpio = 32;
constexpr int kDebounceMs = 500;         // push button debounce period
// Turn blink
constexpr int kTurnBlinkOnMs = 500, kTurnBlinkOffMs = 500;
// Timing
constexpr int kControlLoopHz = 100;
constexpr int kHeartbeatIntervalMs    = 100;   // SYS sends 0x7FE at 10 Hz (fast path for brake loss detection)
constexpr int kHeartbeatTimeoutMsRt   = 1000;  // RT heartbeat loss (0x7FD at 2 Hz, 2 missed frames = 1000ms). Faster 0x204 staleness (200ms) catches RT crash first.
// CAN IDs are in shared/can/can_protocol.h (namespace can):
//   kIdRtHeartbeatLow (0x7FD), kIdSysHeartbeat (0x7FE), kIdRtBrakeCmd (0x205),
//   kIdSyntreeSebCmd (0x7B9), etc.
// Shared constants are in shared/shared_config.h (namespace shared):
//   kSebMaxPressureRaw (100), kStartupGracePeriodMs (3000),
//   kBrakeStrokeScale (0.05), kBrakeStrokeOffset (-30.0)
constexpr int kSetpointStaleMs = 200;           // 0x204 staleness → zero speed
constexpr int kSafetyCheckHz = 20, kGearCheckHz = 50;
// Brake (SYNTREE SEB)
constexpr int kBrakeCmdRateHz = 50, kBrakeBootWaitMs = 500;
constexpr float kBrakeManualStroke = 15.0f, kBrakeMaxStroke = 27.0f;
} // namespace sys
```

### 8.10 Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | ESTOP → DAC=0, gears off, brake=max, **DCDC on** (maintains 12V for MCUs, CAN transceivers, brake light), 12V accessory relay off |
| CAN bus-off | TWAI TEC > 255 | Log, auto-recover |
| RT HB timeout | `0x7FD` alive ctr frozen >1000ms | ESTOP (AUTO only) |
| `0x204` stale | No `0x204` for >200ms | Zero speed + N gear (controlled stop) |
| Brake lever | GPIO2 LOW | Engage brake |
| ADC fail | `adc1_get_raw()==0` | Throttle = 0 |
| Gear sense conflict | Multiple lines HIGH | Treat as N (fail-safe) |
| DCDC CAN TX fail | TWAI TX errors | 12V relay backup cut |
| SEB sync timeout | No `0x721` within 2s of boot | `BRAKE_DEGRADED` — transmits 0x7B9 with lever-based defaults (0mm released, 15mm pressed). Recovers when 0x721 arrives. Lever always functional. |
| SEB checksum fail | SEB rejects frame | Frame dropped; counter still increments |
| SEB stroke following error | abs(0x7B9 cmd − 0x721 actual) > 3mm for >100ms | Log brake fault to 0x600 diag. Set persistent fault flag. Cannot escalate (ESTOP is already max brake) but critical for post-incident analysis. |
| SEB Level 3 fault | `SEB_Error_Status ≥ 3` in 0x721 or 0x731 | Log SEB severe fault to 0x600 diag. Alert rider via brake light flash pattern (if 12V available). |
| 0x721 staleness | No 0x721 for >100ms (10 missed at 100 Hz) | SEB comms lost — log fault. If sustained, treat as brake system offline. |
| MTR ESTOP ACK timeout | `ESTOP_ACTIVE` bit not set in 0x206 fault_flags within 100ms of ESTOP | Log MTR failure to acknowledge. If 0x206 also stale, MTR comms lost. |
| External WDT timeout | TPS3850 MR pin | MCU hardware reset → all outputs safe state |
| Queue full | `xQueueSend` fail | Frame dropped |

### 8.11 Startup

```
 1. can_driver_init()     → TWAI, low-level CAN
 2. mode_manager_init()   → GPIO11 (MODE), GPIO32 (START)
 3. safety_monitor_init() → GPIO1 (ESTOP), GPIO2 (brake lever), WDT GPIO23
 4. throttle_init()       → ADC1_CH5 + I2C + MCP4725 (output=0)
 5. gear_init()           → GPIO12-14 (IN), GPIO33-35 (OUT, LOW)
 6. lights_init()         → GPIO3,6,7 (IN, switches) + GPIO18-22,25-26 (OUT)
 7. power_init()          → GPIO27 (OUT, LOW)
 8. brake_init()          → BRAKE_BOOT_WAIT (500ms) → LISTEN_SYNC (await 0x721) → ACTIVE
 9. dcdc_init()           → CAN 0x012 enable=0
10. Create queues         → can_rx(16), setpoint(4)
11. Create 15 tasks
12. power_task → 12V relay ON (if not ESTOP)
13. dcdc_task → CAN 0x012 enable=1 (if not ESTOP)
14. safety_task starts WDT toggle → external watchdog armed
15. ESP_LOGI("Ready")
```

### 8.12 I/O & Processing Summary

SYS is the safety controller and body control module. It monitors ESTOP, heartbeats, and brake lever; performs EGAS Level 2 motor monitoring; controls the SEB brake actuator; and manages all vehicle body functions (lights, indicators, DC-DC, 12V power, mode bulbs).

**CAN I/O with processing logic:**

| Input (RX) — DLC, fields | Processing | Output (TX) — DLC, fields | Controls |
|---------------------------|------------|---------------------------|----------|
| GPIO1 ESTOP button (NC, active-low) | Hardware ISR → immediate ESTOP: DAC=0, all gear OFF, brake=max, DC-DC ON, 12V accessory OFF | `0x001` SAFETY_ESTOP — DLC=0 → all nodes | Emergency stop |
| GPIO2 brake lever (active-low) | Lever LOW → `0x7B9` stroke = `kBrakeManualStroke` (~15 mm). Driver always wins over AUTO. | `0x7B9` VCU_SEB_REQ — DLC=8, `{u8 ctrl[2], u16 stroke, u16 press, u8 sec, u8 cksum}` → SEB (Stroke Mode) | Brake (MANUAL override) |
| `0x204` RT_DRIVE_CMD — DLC=5, `{i32 speed_mmps, u8 gear}` | AUTO: `abs(speed)/3000 × 4095` → MCP4725 DAC, gear byte → relay. MANUAL: ADC→DAC pass-through. ESTOP: DAC=0, all gear OFF. Stall >200ms → zero+N. | MCP4725 DAC (0–5V) + gear relays (72V D/S/R) | Motor throttle + gear |
| `0x205` RT_BRAKE_CMD — DLC=4, `{i32 brake_pressure_kpa}` | kPa→SEB raw (`kPa × 0.02`), clamp to `kSebMaxPressureRaw` (100). Mode-switch: 0→positive → Pressure Mode (bit=1); positive→0 → Stroke Mode stroke=0. | `0x7B9` VCU_SEB_REQ — DLC=8, `{u8 ctrl[2], u16 stroke, u16 press, u8 sec, u8 cksum}` → SEB (Pressure Mode) | Brake pressure (AUTO) |
| `0x206` MTR_MOTOR_FBK — DLC=4, `{i16 actual_speed, u8 gear_state, u8 fault_flags}` | EGAS L2: compare 0x204 setpoint vs 0x206 actual. Mismatch → ESTOP. `ESTOP_ACTIVE` bit (0x01) → force ESTOP. | `0x001` SAFETY_ESTOP — DLC=0 if mismatch | Motor safety (EGAS L2) |
| `0x7FD` RT_HEARTBEAT — DLC=1, `{u8 alive_ctr}` | 1000ms timeout (2 missed at 2 Hz) → ESTOP. Faster: 0x204 staleness at 200ms catches RT crash first. | `0x001` SAFETY_ESTOP — DLC=0 if timeout | RT liveness |
| GPIO11 MODE button (active-low, debounced) | Toggle MANUAL ↔ AUTO on falling edge. Ignored in ESTOP. | `0x110` SYS_MODE_CMD — DLC=1, `{u8 mode (0=M, 1=A, 2=ESTOP)}` → RT + MTR | Mode control |
| GPIO32 START button (active-low, debounced) | ESTOP → MANUAL on falling edge. No effect in AUTO/MANUAL. Long-press (3s) secondary ESTOP exit (gap #11). | `0x110` SYS_MODE_CMD — DLC=1, `{u8 mode}` → RT + MTR | ESTOP exit |
| `0x302` HOST_LIGHT_CMD — DLC=1, `{u8 lights bitfield}` (fwd from RT) | Lights bitfield → GPIO relay outputs. AUTO: from CAN. MANUAL: handlebar switches (GPIO 3/6/7). ESTOP: all OFF except brake. | GPIO 18 (L turn), 19 (R turn), 21 (brake), 22 (head) | Signal lights |
| `0x721` SEB_STATUS — DLC=8, `{u8 status, u16 stroke, u16 angle, u8 press, …}` | Brake SM: boot sync stroke, check `SEB_Alignment_Status==1`, following error >3 mm for >100 ms → fault log. `SEB_Error_Status≥3` (L3 fault) → ESTOP. | `0x7B9` VCU_SEB_REQ — DLC=8, `{u8 ctrl[2], u16 stroke, u16 press, u8 sec, u8 cksum}` (50 Hz, rolling counter + checksum) | Brake actuator |
| `0x731` SEB_ErrInfo — DLC=8, `{23 fault flags (16× L3)}` | L3 fault → ESTOP via `0x001` | — (consumed locally) | Brake health |
| `0x741` SEB_Version — DLC=8, `{u8 sw_ver, u8 hw_ver}` | Log on boot for compatibility check | — (consumed locally) | Brake health |
| `0x6FB` SEB_Test — DLC=8, `{i16 mtr_curr, u16 ecu_temp, u16 pow_volt}` | Monitor motor current / ECU temp trends for degradation early warning | — (consumed locally) | Brake health |
| — | DC-DC ON in all modes (MANUAL, AUTO, ESTOP). Sent on state change. | `0x012` SYS_DCDC_CMD — DLC=1, `{u8 enable}` → DC-DC converter | DC-DC 72V→12V |
| — | Diagnostics aggregation: system health, faults | `0x600` SYS_DIAG_RPT — DLC=8, `{diag struct}` → RT (fwd to Jetson), 1 Hz | System diagnostics |
| — | Heartbeat generation: alive_ctr++, 10 Hz (fast path for brake loss detection) | `0x7FE` SYS_HEARTBEAT — DLC=1, `{u8 alive_ctr}` → RT, 10 Hz | Liveness |
| `0x011` SYS_SAFETY_STS generation | Pack estop state, heartbeat OK, light state | `0x011` SYS_SAFETY_STS — DLC=3, `{u8 estop, u8 hb_ok, u4 light_state}` → RT (fwd to Jetson), 5 Hz | Safety status |
| GPIO3 L-turn switch, GPIO6 R-turn switch, GPIO7 headlight switch (MANUAL) | Turn L/R toggle, headlight toggle, both→hazard. Blink 500ms on/off pattern. Pressing opposite cancels. | GPIO 18 (L), 19 (R), 22 (head) relay outputs | Manual lights |

**Physical I/O (SYS board):**

| Signal | GPIO | Type | Purpose |
|--------|------|------|---------|
| CAN (low) | TX=5, RX=4 | TWAI | Low-level CAN bus |
| ESTOP button | 1 | In, NC, active-low, ISR | Emergency stop (shared with MTR hardwire) |
| Brake lever | 2 | In, active-low, pull-up | Manual brake trigger |
| START button | 32 | In, active-low, pull-up, debounced | Exit ESTOP → MANUAL |
| MODE button | 11 | In, active-low, pull-up, debounced | Toggle MANUAL ↔ AUTO |
| Left turn switch | 3 | In, active-low, pull-up | Handlebar left turn |
| Right turn switch | 6 | In, active-low, pull-up | Handlebar right turn |
| Headlight switch | 7 | In, active-low, pull-up | Handlebar headlight toggle |
| Left turn relay | 18 | Out | Left turn lamp |
| Right turn relay | 19 | Out | Right turn lamp |
| Brake light relay | 21 | Out | Brake light (always-on DC-DC rail, not accessory) |
| Headlight relay | 22 | Out | Headlight |
| AUTO bulb | 25 | Out | AUTO mode indicator (relay → 12V bulb) |
| MANUAL bulb | 26 | Out | MANUAL mode indicator (relay → 12V bulb) |
| 12V accessory relay | 27 | Out | Secondary power cut on ESTOP |
| WDT toggle | 23 | Out, 20 Hz | TPS3850 external watchdog |

**What SYS manipulates:** ESTOP state (from GPIO1, CAN 0x001, or RT heartbeat loss), mode state (from MODE button or START button), brake command (stroke from lever or pressure from 0x205), motor setpoint (from 0x204 or ADC pass-through), light state (from 0x302 or handlebar switches), gear state (from 0x204 or TLP281 mirroring), DC-DC enable (always ON).

**What SYS controls:** SEB brake actuator via `0x7B9` (Stroke Mode for lever/ESTOP, Pressure Mode for AUTO 0x205), motor throttle via MCP4725 DAC (0–5V analog), motor gear via relay module (72V D/S/R lines), DC-DC converter via `0x012`, 12V accessory relay via GPIO27 (OFF on ESTOP), signal lights via GPIO 18/19/21/22, mode indicator bulbs via GPIO 25/26.

---

## 9. CAN bus device maps

### Low-level

```
 Low-Level CAN (500 kbit/s)
  ├── RT ESP32-S3 (TWAI)        TX: 0x169,0x204,0x205,0x302,0x001,0x7FD
  │                              RX: 0x001,0x011,0x110,0x120,0x201,0x202,0x203,0x206,0x600,0x6FA,0x7FD,0x7FE
  ├── SYS ESP32-S3               TX: 0x011,0x012,0x110,0x600,0x7B9,0x001,0x7FE
  │                              RX: 0x001,0x204,0x205,0x206,0x302,0x6FB,0x721,0x731,0x741,0x7FD,0x7FB
  ├── PWT ESP32-S3 (TWAI0)      TX: 0x7FB | RX: 0x001,0x012,0x7FD,0x7FE
  ├── SYNTREE EPS-C (steering)   TX: 0x201 | RX: 0x169
  ├── SYNTREE SEB (brake)        TX: 0x721 | RX: 0x7B9
  └── MTR STM32 (motor)          TX: 0x120,0x206 | RX: 0x001,0x110,0x204,0x7FD
```

### High-level

```
 High-Level CAN (500 kbit/s)
  ├── Jetson Orin             TX: 0x300,0x301,0x302,0x001,0x7FC,0x400
  │                              RX: 0x001,0x011,0x120,0x206,0x210,0x220,0x310,0x311,0x600,0x7FD
  └── RT ESP32-S3 (MCP2515)      TX: 0x011,0x120,0x206,0x210,0x220,0x310,0x311,0x600,0x001,0x7FD
                                  RX: 0x001,0x300,0x301,0x302,0x400,0x7FC
```

### Powertrain

```
 Powertrain CAN (250 kbit/s)
  ├── PWT ESP32-S3 (TWAI1)      TX: 0x012,0x001
  │                              RX: 0x001, TBD (motor controller)
  ├── DC-DC converter (72→12V)  RX: 0x012
  └── Motor controller           TX: TBD (telemetry) | Analog in: 0-5V throttle, 72V gear (from MTR)
```

---

## 10. Hardware summary

| Node | Controller | MCU | Framework | CAN interfaces |
|------|-----------|-----|-----------|---------------|
| Jetson | Orin | — | ROS 2 | 1× CAN (high) |
| RT | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI (low) + MCP2515 SPI (high) |
| SYS | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI (low only) |
| PWT | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI0 (low) + TWAI1 (powertrain) |

| Parameter | Value |
|-----------|-------|
| CAN bitrate (high, low) | 500 kbit/s |
| CAN bitrate (powertrain) | 250 kbit/s |
| CAN transceiver | SN65HVD230 |
| FreeRTOS tick | 1000 Hz |
| RT tasks | 8 |
| SYS tasks | 15 |
| PWT tasks | 5 |

---

## 11. Build

```bash
cd rt-esp32 && pio run && pio run -t upload && pio device monitor
cd sys-esp32 && pio run && pio run -t upload && pio device monitor
cd pwt-esp32 && pio run && pio run -t upload && pio device monitor
```

---

## 12. Known design gaps

| # | Gap | Impact | Resolution |
|---|-----|--------|------------|
| 1 | ~~Brake arbitration has no CAN path to SYS~~ | ~~Jetson `0x301` + RT obstacle braking never actuated~~ | **RESOLVED:** `0x205 RT_BRAKE_CMD` (RT→SYS, DLC=4, i32 kPa, 50 Hz). SYS maps kPa→SEB Pressure Mode. Mode-switching protocol defined in §8.6. |
| 2 | ~~No CAN message for Jetson to request S (Sport) gear~~ | ~~AUTO can only select D/N/R~~ | **RESOLVED:** `0x300 HOST_DRIVE_CMD` repacked: i32 speed + i24 yaw + u8 gear (N=0,D=1,S=2,R=3). RT passes gear through to `0x204`. |
| 3 | ~~EPS-C timeout-fault behavior unknown~~ | ~~On ESTOP or comm loss, steering may lock, center, or freewheel~~ | **RESOLVED:** Two-tier ESTOP steering (§7.6). Active-zero centering for non-obstacle ESTOP with silent-stop fallback on mechanical jam. Obstacle-triggered ESTOP holds current angle. EPS-C timeout-fault is now a last-resort fallback (CAN bus dead), not the primary ESTOP mechanism. |
| 4 | ~~SEB pressure control mode not defined~~ | ~~SYS currently uses stroke mode only~~ | **RESOLVED:** Verified SYNTREE SEB spec: `VCU_SEB_Pre_Value_Req` is u8 at bit 32, scale 0.05 MPa/bit, range 0–5 MPa (raw 0–100). Conversion: `seb_raw = kPa × 0.02`. Clamp to 100. |
| 5 | Speed control open-loop — PID exists but encoder not fitted | `speed_pid.cpp` is correct but runs with `measured=0` (encoder not physically installed). PID must NOT be in the active control path until GPIO1/2 PCNT is wired. Currently runs as shadow controller → `0x220` telemetry for validation. | 1) Fit rear motor encoder to GPIO1/2. 2) Verify encoder pulses on PCNT. 3) Enable `pid_update(desired, measured, dt)` with real data. 4) Route PID effort trim to SYS via new field in `0x204` or dedicated ID. 5) Validate closed-loop response against open-loop baseline. |
| 6 | ~~ESTOP exit race condition — START button during steering ramp~~ | ~~Pressing START before non-obstacle centering ramp completes stops 0x169 mid-ramp, leaving EPS-C to comm-fault at an off-center angle.~~ | **RESOLVED:** `exit_estop()` sets `m_estop_exit_pending` flag instead of immediate transition. `tick()` checks flag after ramp/hold completes → ACTIVE. Brake/motor/lights transition immediately. |
| 7 | **SEB BRAKE_FAULT makes physical brake lever inoperative** | Original BRAKE_FAULT state permanently stops 0x7B9 transmission on boot sync timeout. Since SEB is by-wire (electro-hydraulic, no mechanical linkage), this disables the brake lever — contradicting the claim "brake lever always works." | **Software fix:** Replaced BRAKE_FAULT with BRAKE_DEGRADED (§8.6). On sync timeout, transmit 0x7B9 with lever-based defaults (0mm/15mm) without waiting for sync. Recovers when 0x721 arrives. Lever always functional. See `docs/emergency-safety-analysis.md` §2. |
| 8 | **Watchdog reset unbraked window — SEB comm-fault behavior unverified** | When SYS watchdog resets, SEB enters comm-fault after 20ms. If SEB releases on timeout, the vehicle coasts without brake for ~2.5s (SYS reboot + brake LBS). If SEB holds, the window is only 20ms. Behavior is empirically unverified. | **Test SEB comm-fault behavior** (stroke=27mm, stop CAN, measure pressure over 5s). If release: add NC brake-hold relay gated by TPS3850 RST line. If hold: document as verified safety property. **Also mitigated by gap #11 — RT brake takeover closes most of the window.** See `docs/emergency-safety-analysis.md` §3. |
| 9 | **Obstacle ESTOP "hold angle" can cause rollover during cornering** | Obstacle-triggered ESTOP holds current steering angle regardless of vehicle speed. A cornering vehicle under hard braking experiences lateral load transfer that the dynamic angle clamp was designed to prevent — but the clamp is only applied to commanded angles, not to the ESTOP hold angle. Physics model §8 defines the rollover threshold: a_y = v²/L·tan(δ) > g·w/(2h). | **Software fix:** During obstacle-triggered ESTOP, clamp the hold angle to the dynamic angle clamp limit for the current speed. If current angle exceeds the limit, ramp down to the limit at 20°/s. Straight-line cases unchanged. See `docs/emergency-safety-analysis.md` §4. |
| 10 | **Jetson heartbeat loss → pure coast is insufficient for perception failure** | Jetson runs obstacle detection. When it dies, the vehicle coasts with no active brake, no perception, and no steering. At 25 km/h, >50m coast-to-stop. The rider must recognize the failure and manually brake. The heartbeat timeout (1500ms) is long enough that false positives from Linux CAN jitter are unlikely. | **Software fix:** On Jetson heartbeat loss, RT commands 0x205 = 2000 kPa (~2 MPa moderate brake) + transitions SYS to MANUAL. Brake light ON. Rider can override with lever. DC-DC stays on (lights work). This is "assisted stop" — between coast and full ESTOP. See `docs/emergency-safety-analysis.md` §5. |
| 11 | **No secondary ESTOP exit path — START button is a single point of failure** | GPIO32 is the only ESTOP exit. Stuck HIGH (broken wire) = can never exit ESTOP. The only backup is power-cycle, which restarts all nodes and requires brake/steering LBS at roadside. | **Software fix:** MODE button (GPIO11) long-press (3s) exits ESTOP → MANUAL as secondary path. Two independent GPIOs on separate physical buttons. Add START button health monitoring in diag_task. See `docs/emergency-safety-analysis.md` §6. |
| 12 | ~~SYS crash — 1000ms brake gap before RT responds~~ | ~~Architecture §6.2 Option D claims RT takes over brake but only 0x001 was sent, which SEB ignores.~~ | **RESOLVED:** (1) SYS heartbeat 2→10 Hz; RT timeout 200ms. (2) RT takes over 0x7B9 with stroke=max on SYS loss (emergency). (3) In normal AUTO, SYS suppresses its 0x7B9 — RT sends directly (1-hop). SYS resumes on lever press, ESTOP, or RT heartbeat loss. `0x205` becomes monitoring-only for EGAS L2. Brake gap: 220ms worst case. |
| 13 | **No independent brake monitor — ESTOP could silently have no brakes** | All 8 safety layers protect motor+steering; none verify SEB actually applied braking force. If SEB's CAN receiver is faulted, ESTOP `0x7B9 stroke=max` is never received and the system never knows. SEB_STATUS (`0x721`, 100 Hz) already provides SEB_Stroke_Value, SEB_Pressure_Value, and SEB_Error_Status (Level 3 = severe/shutdown). This is sufficient for a brake following-error monitor with zero new sensors. | **Software fix:** Add brake following-error check in SYS `dispatch`: compare 0x7B9 cmd stroke vs 0x721 actual stroke (threshold 3mm, debounce 100ms). Monitor SEB_Error_Status for Level 3 faults. Add 0x721 staleness check (100ms timeout). Faults logged via 0x600 diagnostic — cannot escalate beyond ESTOP (already max brake) but essential for incident analysis. See `docs/emergency-safety-analysis.md` §8. |
| 14 | ~~CAN 0x001 spoofable — no authentication, DoS-vulnerable~~ | ~~0x001 DLC=0 with no rate limiting. Corrupted node could flood ESTOP frames.~~ | **RESOLVED:** 250ms minimum interval between 0x001 broadcasts on both RT and SYS (`can_send_estop()` rate limiter). Max 2 frames per 500ms window per node. DLC=1 with sender-ID byte deferred to future protocol change. |
| 15 | ~~No ESTOP acknowledgment from MTR STM32~~ | ~~SYS/RT have no confirmation that MTR received and acted on ESTOP.~~ | **RESOLVED:** MTR sets `ESTOP_ACTIVE` bit (0x01) in 0x206 fault_flags. SYS dispatch checks bit on receipt → force_estop if set. Fault flag constants centralized in `shared/shared_config.h` (`kMtrFault*`). MTR 0x206 staleness check already implemented (>200ms). |
| 16 | ~~Startup grace period masks heartbeat but not 0x204 staleness~~ | ~~SYS 0x204 staleness check triggered on cold boot before RT online.~~ | **RESOLVED:** SYS `task_motor` now gates 0x204 staleness check with 3s startup grace (`startup_grace` flag). MTR already had this guard. Both nodes now consistent. |
| 17 | **ESTOP HMI ambiguous — "both bulbs OFF" identical to powered-off vehicle** | During ESTOP, DC-DC is OFF → 12V rail dead → brake light, mode bulbs, and indicators all dark. A rider returning to the vehicle cannot distinguish ESTOP from power-off. The OR logic claim "brake light ON during ESTOP" is physically impossible with DC-DC off. | **Software + wiring fix:** Keep DC-DC ON during ESTOP (needed for MCU power anyway). Cut only the 12V accessory relay (GPIO27). Rewire brake light to always-on DC-DC rail (not accessory relay output). Result: ESTOP = brake light ON + mode bulbs OFF. Power-off = everything OFF. See `docs/emergency-safety-analysis.md` §12. |
| 18 | **EPS-C mechanical jam silent-stop recovery path unclear** | When mechanical jam triggers silent-stop during ESTOP centering, the architecture says "fall back to silent-stop" without specifying steer SM state transition. It's unclear whether the jam is recoverable via START short-press (STEER_FAULT path) or requires power-cycle. | **Documentation fix:** Explicitly transition to STEER_FAULT on mechanical jam during ESTOP centering. Existing STEER_FAULT recovery paths apply: START short-press → LISTEN_SYNC retry; START long-press 3s + throttle=0 → force-activate at 0° (MANUAL only). See `docs/emergency-safety-analysis.md` §13. |
| 19 | **Fixed 5° steering following error threshold wrong at high speed** | At 25 km/h, dynamic clamp limits steering to ~5°. A fixed 5° threshold represents a 100% error — the EPS-C must have ZERO response to trigger. A 4° error at speed (80% authority loss) would NOT trigger ESTOP. At 2 km/h (40° limit), 5° is only 12.5% — potentially too sensitive for parking maneuvers. | **Software fix:** Speed-scaled threshold: `max(2°, 0.25 × dynamic_limit)`. Result: 2° at 25 km/h (tight), 4.5° at 10 km/h, 10° at 2 km/h (tolerant). One-line change in RT following-error check. See `docs/emergency-safety-analysis.md` §14. |
| 20 | **Motor controller CAN protocol undocumented** | Motor controller outputs telemetry on the 250k powertrain CAN bus (speed, current, temperature, fault flags). Specific CAN IDs, signal layouts, and update rates are unknown — depends on motor controller model selection. Until documented, PWT cannot parse or forward motor telemetry to the 500k bus. | 1) Identify motor controller model and obtain CAN protocol documentation. 2) Define CAN IDs and signal layouts in `shared/can/can_signals.yaml`. 3) Implement PWT motor telemetry parsing and forwarding. 4) Update `pwt-esp32/pwt-architecture.md` with confirmed IDs. |

---

## 13. Reference documents

| File | Content |
|------|---------|
| [`can-dictionary.md`](can-dictionary.md) | Bit-level CAN signal layouts for all IDs on both buses |
| [`docs/steering-unit.md`](docs/steering-unit.md) | SYNTREE EPS-C protocol reference |
| [`docs/brake-unit.md`](docs/brake-unit.md) | SYNTREE SEB protocol reference |
| [`rt-esp32/README.md`](rt-esp32/README.md) | RT build & test |
| [`sys-esp32/README.md`](sys-esp32/README.md) | SYS build & test |
| [`notes/can-protocol.md`](notes/can-protocol.md) | CAN protocol theory — arbitration, frame types, standards |
| [`notes/can-hardware-basics.md`](notes/can-hardware-basics.md) | CAN physical layer — termination, topology, transceivers |
| [`notes/can-troubleshooting.md`](notes/can-troubleshooting.md) | CAN debugging — common mistakes, error states, tools |
| [`notes/rtos-task-design.md`](notes/rtos-task-design.md) | RTOS task design — priorities, preemptive scheduling, queues, tick rate |
| [`notes/pid-control.md`](notes/pid-control.md) | PID control fundamentals — P/I/D terms, integral windup, tuning procedure |
| [`notes/state-machine-design.md`](notes/state-machine-design.md) | State machine design — FSMs, hierarchical states, C++ implementation patterns |
| [`notes/heartbeat-monitoring.md`](notes/heartbeat-monitoring.md) | Heartbeat & liveness — alive counters, FTTI, timeout selection, startup grace |
| [`notes/distributed-safety-patterns.md`](notes/distributed-safety-patterns.md) | Distributed safety — defense in depth, ESTOP, fail-safe, NC wiring, debounce |
| [`notes/endianness-binary-protocols.md`](notes/endianness-binary-protocols.md) | Endianness & binary protocols — big/little endian, bit numbering, struct packing |
| [`notes/can-gateway-design.md`](notes/can-gateway-design.md) | CAN gateway design — multi-bus architecture, forwarding categories, isolation |
| [`notes/analog-interfacing.md`](notes/analog-interfacing.md) | Analog interfacing — ADC, DAC, optoisolators, relays, protection circuits |
| [`docs/physics-model.md`](docs/physics-model.md) | Tricycle kinematics — forward/inverse, rollover, slip angles |
| [`docs/listen-before-speaking.md`](docs/listen-before-speaking.md) | CAN actuator safe bootstrapping pattern |
| [`docs/can-gateway-bridging.md`](docs/can-gateway-bridging.md) | CAN gateway forwarding rules and implementation |
| [`docs/emergency-system.md`](docs/emergency-system.md) | **Primary:** ESTOP triggers, 8-layer defense, emergency response matrix, rider's guide, EGAS 3-level architecture, testing procedures |
| [`docs/emergency-safety-analysis.md`](docs/emergency-safety-analysis.md) | **Safety analysis:** ESTOP exit race condition, SEB brake lever contradiction, watchdog unbraked window — causal traces, risk quantification, recommended fixes |
| [`docs/defense-in-depth-safety.md`](docs/defense-in-depth-safety.md) | Layered safety — ESTOP, following error, dynamic clamp, OR logic |
| [`docs/syntree-security-protocol.md`](docs/syntree-security-protocol.md) | Rolling counter + XOR checksum for SYNTREE actuators |
| [`docs/high-voltage-isolation.md`](docs/high-voltage-isolation.md) | 72V galvanic isolation — TLP281 optos, relays, fuses, TVS |
| [`docs/distributed-architecture.md`](docs/distributed-architecture.md) | Three-node rationale — Jetson/RT/SYS split, dual-CAN on RT |
| [`docs/actuator-interfacing.md`](docs/actuator-interfacing.md) | MCP4725 DAC throttle, gear pass-through, relay logic |
| [`docs/external-watchdog.md`](docs/external-watchdog.md) | External watchdog IC — timeout, safe state, testing |
| [`docs/pid-speed-control.md`](docs/pid-speed-control.md) | PID speed control theory, tuning, anti-windup |
