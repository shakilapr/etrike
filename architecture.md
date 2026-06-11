# E-Trike System Architecture

Three-node distributed control: **Jetson Orin NX** (ROS 2 perception/planning), **RT ESP32-S3** (realtime physics, steering & CAN gateway), **SYS ESP32-S3** (safety, motor actuation & body control).

Two physical CAN buses at 500 kbit/s. RT is the only node on both buses and bridges selected messages. Actuators are **SYNTREE** CAN modules: EPS-C (steer-by-wire) and SEB (electro-hydraulic brake).

---

## 1. Topology

```
  ┌────────────────── High-Level CAN (500 kbit/s) ──────────────────┐
  │                                                                  │
  │  ┌──────────┐            ┌──────────────┐                       │
  │  │  Jetson  │            │  RT ESP32-S3 │                       │
  │  │  Orin NX │            │              │                       │
  │  │          │            │ Physics      │                       │
  │  │ ROS 2    │            │ Steering     │                       │
  │  │ Planning │            │ PID          │                       │
  │  └────┬─────┘            │ CAN Gateway  │                       │
  │       │                  └──────┬───────┘                       │
  │  TX:  0x300,0x301,    TX: 0x011,0x120, │                        │
  │       0x302,0x001           0x210,0x220,│                        │
  │                              0x400,0x600,│                       │
  │  RX:  0x011,0x120,          0x001,0x7FF │                        │
  │       0x210,0x220,      RX: 0x300,0x301,│                        │
  │       0x400,0x600,          0x302,0x001 │                        │
  │       0x001,0x7FF                       │                        │
  └─────────────────────────────────────────┘                        │
                                            │                        │
                 ┌──────────────────────────┘                        │
                 │                                                    │
  ┌──────────────▼─────── Low-Level CAN (500 kbit/s) ───────────────┐│
  │                                                                  ││
  │  ┌──────────────┐      ┌──────────────┐                         ││
  │  │  RT ESP32-S3 │      │ SYS ESP32-S3 │                         ││
  │  │  (gateway)   │      │              │                         ││
  │  └──────┬───────┘      │ Safety       │                         ││
  │         │              │ Motor        │                         ││
  │    TX:  0x200,0x202,   │ Brake        │                         ││
  │         0x302,0x001,   └──────┬───────┘                         ││
  │         0x7FF                 │                                  ││
  │                          TX:  0x720,0x011,                      ││
  │    RX:  0x001,0x011,          0x012,0x110,                      ││
  │         0x110,0x120,          0x120,0x600,                      ││
  │         0x201,0x600,          0x001,0x7FF                       ││
  │         0x7FF              RX:  0x001,0x200,                    ││
  │                               0x302,0x721,                      ││
  │                               0x7FF                             ││
  └─────────────────────────────────────────────────────────────────┘│
                                        │                            │
                ┌───────────────────────┼────────────────────┐       │
                │                       │                    │       │
          ┌─────▼─────┐          ┌─────▼─────┐        ┌─────▼─────┐ │
          │  SYNTREE  │          │  SYNTREE  │        │   DC-DC   │ │
          │  SEB      │          │  EPS-C    │        │ Converter │ │
          │  (Brake)  │          │ (Steering)│        │ 72V→12V   │ │
          │ 0x720 cmd │          │ 0x200 cmd │        │ (0x012)   │ │
          │ 0x721 stat│          │ 0x201 stat│        └───────────┘ │
          └───────────┘          └───────────┘                      │
                                                                    │
  ┌───────────┐                                                    │
  │  Motor    │  (analog: 0–5 V throttle via MCP4725,              │
  │Controller │   72 V gear lines via relay module, from SYS)      │
  └───────────┘                                                    │
```

---

## 2. CAN message catalog

### 2.1 Low-level CAN

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Prio |
|----|------|--------|-------------|-----|---------|--------|------|
| `0x001` | SAFETY_ESTOP | Any | All (bridged to high) | 0 | (none) | Event | Highest |
| `0x011` | SYS_SAFETY_STATUS | SYS | RT (→ Jetson) | 2 | u8 estop, u8 hb_ok | 5 Hz | V.High |
| `0x012` | SYS_DCDC_CMD | SYS | DC-DC converter | 1 | u8 enable | Change | V.High |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | u8 mode (0=M, 1=A) | Change | High |
| `0x120` | SYS_THROTTLE_POS | SYS | RT (→ Jetson) | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x200` | VCU_SES_REQ | RT | EPS-C (steering) | 8 | Angle cmd + security bytes | 50 Hz | Medium |
| `0x201` | SES_STATUS | EPS-C | RT | 8 | Steering angle + status feedback | 100 Hz | Medium |
| `0x202` | RT_DRIVE_SETPOINT | RT | SYS | 5 | i32 speed_mmps, u8 gear | 100 Hz | Medium |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | SYS | 1 | u8 lights bitfield | Change | Medium |
| `0x600` | SYS_DIAG | SYS | RT (→ Jetson) | 8 | diag struct | 1 Hz | Lowest |
| `0x720` | VCU_SEB_REQ | SYS | SEB (brake) | 8 | Stroke/pressure cmd + security | 50 Hz | Medium |
| `0x721` | SEB_STATUS | SEB | SYS | 8 | Brake stroke + status feedback | 100 Hz | Medium |
| `0x7FF` | HEARTBEAT | RT, SYS | RT, SYS | 1 | u8 alive_ctr | 2 Hz | Lowest |

> **ID note**: SYNTREE units are preprogrammed and cannot be reconfigured. EPS-C uses factory command `0x200` and status `0x201`. SEB uses factory command `0x720` and status `0x721`. `RT_DRIVE_SETPOINT` is placed at `0x202` to avoid collision with EPS-C `0x200`.

### 2.2 High-level CAN

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Prio |
|----|------|--------|-------------|-----|---------|--------|------|
| `0x001` | SAFETY_ESTOP | RT (fwd), Jetson | Jetson, RT | 0 | (none) | Event | Highest |
| `0x011` | SYS_SAFETY_STATUS | RT (fwd) | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz | V.High |
| `0x120` | SYS_THROTTLE_POS | RT (fwd) | Jetson | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x210` | RT_STATE_REPORT | RT | Jetson | 3 | u8 mode, u8 steer_valid, u8 reversing | 10 Hz | Low |
| `0x220` | RT_PID_FEEDBACK | RT | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | i32 speed_mmps, i32 yaw_rate_mrad_s | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | RT | 4 | i32 brake_pressure_kpa | Demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→ SYS) | 1 | u8 lights bitfield | Change | Medium |
| `0x400` | RT_OBSTACLE_DIST | RT | Jetson | 4 | u32 distance_mm | 10 Hz | Low |
| `0x600` | SYS_DIAG | RT (fwd) | Jetson | 8 | diag struct | 1 Hz | Lowest |
| `0x7FF` | HEARTBEAT | RT, Jetson | RT, Jetson | 1 | u8 alive_ctr | 2 Hz | Lowest |

> Bit-level signal layouts in [`can-dictionary.md`](can-dictionary.md).

### 2.3 CAN gateway forwarding (RT)

| Direction | IDs forwarded | Notes |
|-----------|--------------|-------|
| Low → High | `0x001`, `0x011`, `0x120`, `0x600` | Transparent — same ID, same payload |
| High → Low | `0x001`, `0x302` | Transparent |

**Not forwarded**: `0x300`, `0x301` (consumed by RT); `0x200`, `0x202` (RT-generated on low); `0x210`, `0x220`, `0x400` (RT-generated on high); `0x012`, `0x110`, `0x720` (low only); `0x201`, `0x721` (low only, SYNTREE feedback); `0x7FF` (per-bus, alive counter, not bridged).

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
| **MANUAL** | Rider steers / rides throttle. SYS reads throttle ADC + gear sense → pass-through via MCP4725 + relays. Brake lever → SYS GPIO → CAN `0x720` → SEB. EPS-C standalone (RT idle). DC-DC on. |
| **AUTO** | Jetson `/cmd_vel` → high CAN `0x300` → RT kinematics + PID → low CAN `0x202` (SYS: speed+gear) + `0x200` (EPS-C: angle). SYS drives MCP4725 + gear relays. Lights from Jetson via `0x302` (RT fwd). Brake via `0x720`. |
| **ESTOP** | MCP4725 = 0 V, all gear outputs OFF, `0x720` stroke=max (full brake), `0x200` stops, DC-DC off (`0x012`), 12V relay OFF. Exit: **START button** → MANUAL, or power-cycle. |

---

## 4. Signal flow

### 4.1 Manual mode

```
Throttle grip (0–5V) ──► SYS ADC ──► SYS MCP4725 (0–5V) ──► Motor controller
Gear selector (72V)  ──► TLP281 opto → SYS GPIO ──► relay module → 72V → ECU
Brake lever           ──► SYS GPIO ──► CAN 0x720 → SEB (stroke=MAX if pressed)
Steering wheel        ──► EPS-C standalone (RT idle, monitors 0x201)
Signal lights         ──► Turn/head: rider switches (TBD). Brake: OR logic (lever + ESTOP) → GPIO21
DC-DC converter       ──► SYS CAN 0x012 enable=1 → 12V rail on
```

### 4.2 Auto mode

```
Jetson /cmd_vel ──► High CAN 0x300 ──► RT kinematics + PID
                                          │
               ┌──────────────────────────┤
               ▼ (low CAN)                ▼ (low CAN)
   0x202 {speed, gear} → SYS        0x200 {angle} → EPS-C
               │                          │
               ├──► MCP4725 → Motor       │  RT listens 0x201 for feedback
               ├──► Relays → ECU gear     │  Dynamic angle clamp + following error
               └──► GPIO → Signal lights (turn/head from 0x302; brake = OR logic)

Jetson ──► High CAN 0x301 ──► RT brake arbitration (TBD path to SYS)
Jetson ──► High CAN 0x302 ──► RT fwd → Low CAN 0x302 → SYS → light relays
SYS ────► Low CAN 0x720 → SEB (50 Hz continuous: stroke control)
```

---

## 5. Responsibility split

| Concern | Jetson | RT | SYS |
|---------|--------|-----|-----|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| CAN gateway (low ↔ high) | | ✓ | |
| Tricycle kinematics | | ✓ | |
| Speed PID | | ✓ | |
| Steering angle compute + CAN TX (`0x200`) | | ✓ | |
| Steering boot sync (Listen-Before-Speaking) | | ✓ | |
| Steering safety: dynamic angle clamp, hard-stops, following error | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + button | | | ✓ |
| Brake lever → CAN (`0x720`, 50 Hz continuous) | | | ✓ |
| Brake boot sync (Listen-Before-Speaking) | | | ✓ |
| Brake rolling counter + checksum | | | ✓ |
| DC-DC converter CAN control (`0x012`) | | | ✓ |
| Heartbeat monitoring | | ✓ (Jetson, high) | ✓ (RT, low) |
| Mode switch reading | | | ✓ (push button, GPIO11) |
| Throttle ADC read (0–5V) | | | ✓ |
| Throttle MCP4725 DAC output (0–5V) | | | ✓ |
| Gear 72V read (TLP281 opto) | | | ✓ |
| Gear 72V output (relay module) | | | ✓ |
| 12V accessory power relay | | | ✓ |
| Mode indicator lights | | | ✓ |
| Signal lights (turn, brake, head) | | | ✓ |
| System diagnostics | | | ✓ |

---

## 6. Design principles

1. **Queues over shared state.** No mutexes, no semaphores. Thread-safe queue pipes.
2. **ESTOP bypasses queues.** Safety task preempts and writes directly to actuators.
3. **One CAN ID = one sender per bus.** Except heartbeat `0x7FF` where multiple nodes share the ID with distinct alive counters.
4. **Lower CAN ID = higher bus priority.** Safety IDs (`0x00X`) win arbitration.
5. **All multi-byte CAN fields are big-endian (MSB first)** — unless SYNTREE protocol specifies otherwise (see `0x200`, `0x720` in can-dictionary).
6. **Manual mode is pass-through, not dead.** SYS mirrors physical inputs to outputs.
7. **Actuators are standalone CAN modules.** EPS-C, SEB, and DC-DC are commanded via CAN.
8. **RT is the only dual-bus node.** No direct Jetson ↔ SYS path.
9. **Listen Before Speaking.** SYNTREE units require receiving status feedback before any command is sent. Boot state machines enforce this.

---

## 7. RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

### 7.1 Role

Converts ROS 2 motion commands (high CAN `0x300`) into:
- **Speed + gear** → low CAN `0x202` → SYS
- **Steering angle** → low CAN `0x200` → SYNTREE EPS-C

Bridges selected CAN messages (§2.3). Listens to `0x201 SES_STATUS` for steering feedback and safety monitoring.

**9 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

### 7.2 Dual CAN hardware

| Bus | Controller | Interface | GPIO | Transceiver |
|-----|-----------|-----------|------|-------------|
| Low-level | Built-in TWAI | Direct | TX=5, RX=4 | SN65HVD230 |
| High-level | MCP2515 | SPI | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SN65HVD230 |

### 7.3 CAN messages received

| Bus | ID | Name | Payload | Action |
|-----|-----|------|---------|--------|
| Low | `0x001` | SAFETY_ESTOP | — | `mode_set(Estop)`, forward to high |
| Low | `0x011` | SYS_SAFETY_STATUS | `{u8 estop, u8 hb_ok}` | Forward to high |
| Low | `0x110` | SYS_MODE_CMD | `u8 mode` | `mode_set(Manual/Auto)` |
| Low | `0x120` | SYS_THROTTLE_POS | `i16 speed_mmps` | Forward to high |
| Low | `0x201` | SES_STATUS | `{u8 status, i16 angle, …}` (8 bytes) | Steering feedback: sync boot angle, following error check |
| Low | `0x600` | SYS_DIAG | 8 bytes | Forward to high |
| Low | `0x7FF` | SYS HEARTBEAT | `u8 alive_ctr` | Feed SYS alive counter; if frozen for >200ms → ESTOP |
| High | `0x001` | SAFETY_ESTOP | — | `mode_set(Estop)`, forward to low |
| High | `0x300` | HOST_DRIVE_CMD | `{i32 speed, i32 yaw}` | → `cmd_queue` |
| High | `0x301` | HOST_BRAKE_REQUEST | `i32 pressure_kpa` | → atomic store |
| High | `0x302` | HOST_LIGHT_CMD | `u8` bitfield | Forward to low |
| High | `0x7FF` | Jetson HEARTBEAT | `u8 alive_ctr` | Feed Jetson alive counter; frozen >500ms → stale command |

### 7.4 CAN messages sent

| Bus | ID | Name | Payload | Rate |
|-----|-----|------|---------|------|
| Low | `0x001` | SAFETY_ESTOP | — | Event |
| Low | `0x202` | RT_DRIVE_SETPOINT | `{i32 speed, u8 gear}` | 100 Hz |
| Low | `0x200` | VCU_SES_REQ | `{u8 ctrl, i16 angle, u8 speed, u8 sec, u8 cnt+cksum, u8 cksum}` (8 bytes) | **50 Hz** |
| Low | `0x302` | HOST_LIGHT_CMD (fwd) | `u8` bitfield | Change |
| Low | `0x7FF` | RT HEARTBEAT | `u8 alive_ctr` | 2 Hz |
| High | `0x001` | SAFETY_ESTOP (fwd) | — | Event |
| High | `0x011` | SYS_SAFETY_STATUS (fwd) | `{u8 estop, u8 hb_ok}` | 5 Hz |
| High | `0x120` | SYS_THROTTLE_POS (fwd) | `i16 speed_mmps` | 100 Hz |
| High | `0x210` | RT_STATE_REPORT | `{u8 mode, u8 steer_valid, u8 reversing}` | 10 Hz |
| High | `0x220` | RT_PID_FEEDBACK | `{i16 sp, i16 meas, i16 out}` | 10 Hz |
| High | `0x400` | RT_OBSTACLE_DIST | `u32 distance_mm` | 10 Hz |
| High | `0x600` | SYS_DIAG (fwd) | 8 bytes | 1 Hz |
| High | `0x7FF` | RT HEARTBEAT | `u8 alive_ctr` | 2 Hz |

### 7.5 Internal data types

```cpp
enum class Mode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

enum class SteerState : uint8_t {
    STEER_BOOT_WAIT,     // 500ms power-on delay — do NOT transmit
    STEER_LISTEN_SYNC,   // Waiting for 0x201 SES_STATUS, read current angle
    STEER_ACTIVE,        // Normal operation — transmit 0x200 at 50 Hz
    STEER_FAULT          // Timeout or ESTOP — stop transmitting
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

#### Steering — SYNTREE EPS-C via CAN (`0x200`)

**Boot sequence — "Listen Before Speaking":**

```
State machine (steer_state_machine_loop):

STEER_BOOT_WAIT:
  - 500ms delay after power-on
  - DO NOT transmit any 0x200 frames
  - → STEER_LISTEN_SYNC

STEER_LISTEN_SYNC:
  - Wait for 0x201 SES_STATUS frame
  - Extract SES_StrAngle (int16, scale 0.1°/bit → convert to internal mdeg)
  - CRITICAL: Set active_target_angle = current_physical_angle
  - Wait for SES_INF_Angle_Status == 1 (aligned)
  - → STEER_ACTIVE

STEER_ACTIVE:
  - Transmit 0x200 at 50 Hz
  - First frame commands wheels to stay exactly where they are
  - Then follow Jetson targets with dynamic clamp
  - Monitor following error: if |cmd - actual| > threshold for > timeout → ESTOP

STEER_FAULT:
  - Stop transmitting 0x200
  - EPS-C will timeout-fault → locks or goes limp (TBD by unit spec)
```

**Unit conversion** (internal mdeg ↔ SYNTREE decideg):

```
SYNTREE raw = internal_angle_mdeg / 100   (45500 mdeg → 455 raw → 45.5°)
internal_mdeg = SYNTREE raw * 100         (455 raw → 45500 mdeg)
```

**SYNTREE protocol specifics:**

| Parameter | Value |
|-----------|-------|
| Command ID | `0x200` (factory default — SYNTREE preprogrammed, not reconfigurable) |
| Rate | 50 Hz (20 ms period) — continuous transmission required |
| Control mode | 1 = Angle Mode |
| Angle range | ±780 raw (±78.0°, unit limit; software clamp tighter) |
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
| **Following error** | Compare commanded angle (`0x200`) vs actual (`SES_StrAngle` from `0x201`). If abs(error) > 5° for > 300 ms → trigger ESTOP (stuck linkage / rock jam). |
| **Timeout fault** | If `0x200` stops for >20 ms, EPS-C triggers internal comm fault. RT must maintain 50 Hz in AUTO. |
| **Alignment check** | `SES_INF_Angle_Status` must be 1 before AUTO mode engages. Drive motor locked out until aligned. |

**Mode behavior:**

| Mode | Steering behavior |
|------|------------------|
| MANUAL | RT does NOT send `0x200`. EPS-C standalone. RT still listens `0x201` for telemetry. |
| AUTO | RT sends `0x200` at 50 Hz with resolved angle, dynamic clamp + slew rate applied. |
| ESTOP | RT stops sending `0x200`. EPS-C timeout-faults (lock/limp TBD). |

#### Speed PID, Obstacle, Watchdog

Unchanged. Same as prior revision: Kp=1.0, Ki=0.1, Kd=0.05, integral clamp ±500, 100 Hz. Obstacle limit 300–3000 mm. Staleness 500 ms → zero `0x202` + stop `0x200`.

#### Brake arbitration (max-select)

```
brake_kpa = max(rt_computed, jetson_request)
```

**Gap**: arbitrated result has no CAN path to SYS. SYS brake via `0x720` currently driven by ESTOP state + brake lever only (§12).

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
           100 Hz: kinematics + PID + dynamic angle clamp + obstacle + brake → setpoint_queue

Pri 3  can_tx_low   ◀── setpoint_queue + gw_tx_low_queue
           → 0x202 (100 Hz, drive setpoint), 0x200 (50 Hz, steer state machine), 0x302 (change)
      can_tx_high  ◀── telemetry + gw_tx_high_queue → 0x011,0x120,0x210,0x220,0x400,0x600

Pri 2  obstacle     ── HC-SR04 @ 10 Hz → high CAN 0x400

Pri 1  watchdog     ── 10 Hz staleness check
      heartbeat    ── 2 Hz 0x7FF on both buses
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx_low` | 5 | 4096 B | Event | `twai_receive()` → queue |
| `can_rx_high` | 5 | 4096 B | Event | MCP2515 SPI → queue |
| `dispatch` | 4 | 4096 B | Event | Route both RX queues + gateway + steer feedback |
| `control` | 4 | 4096 B | 100 Hz | Kinematics, PID, dynamic angle clamp, obstacle, brake, gear |
| `can_tx_low` | 3 | 3072 B | Event | 0x202@100Hz, 0x200@50Hz (steer SM gated), 0x302 |
| `can_tx_high` | 3 | 3072 B | Event | Telemetry → MCP2515 SPI |
| `obstacle` | 2 | 2048 B | 10 Hz | HC-SR04 → 0x400 |
| `watchdog` | 1 | 2048 B | 10 Hz | Staleness → zero setpoints + stop steer |
| `heartbeat` | 1 | 2048 B | 2 Hz | 0x7FF both buses: `alive_ctr++ & 0xFF`, DLC=1 |

### 7.8 Hardware pin assignments

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX (low) | 5 | Out | SN65HVD230 |
| CAN RX (low) | 4 | In | SN65HVD230 |
| SPI SCK | 36 | Out | MCP2515 (high CAN) |
| SPI MOSI | 37 | Out | MCP2515 |
| SPI MISO | 38 | In | MCP2515 |
| SPI CS | 39 | Out | MCP2515 |
| MCP INT | 40 | In | MCP2515 interrupt |
| Ultrasonic TRIG | 7 | Out | HC-SR04 |
| Ultrasonic ECHO | 8 | In | HC-SR04 |
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
| WDT toggle | 21 | Out | External watchdog IC (TPS3850). Toggled by `control_task` every 10 ms. |

### 7.9 Configuration constants

```cpp
namespace rt {
// Vehicle
constexpr float kWheelbaseMM = 1500.0f;
// Steering (SYNTREE EPS-C)
constexpr float kSteerHardLimitDeg = 40.0f;       // software hard-stop inside mechanical limit
constexpr float kSteerFollowingErrDeg = 5.0f;     // following error threshold
constexpr int   kSteerFollowingErrMs = 300;        // duration before ESTOP
constexpr int   kSteerCmdRateHz = 50;              // SYNTREE requires 20ms period
constexpr int   kSteerBootWaitMs = 500;            // power-on delay before listening
// Dynamic angle clamp
constexpr float kSteerMaxAngleAtLowSpeed = 40.0f;  // at 2 km/h
constexpr float kSteerMaxAngleAtHighSpeed = 5.0f;  // at 25 km/h
// Speed limits
constexpr int kMaxSpeedFwdMmps = 3000, kMaxSpeedRevMmps = 500;
constexpr int kLowSpeedThreshMmps = 50;
// PID
constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f, kPidMaxIntegral = 500.0f;
// Obstacle
constexpr unsigned kObstacleStopMM = 300, kObstacleClearMM = 3000;
// Timing
constexpr int kControlLoopHz = 100, kCmdStaleTimeoutMs = 500;
constexpr int kHeartbeatIntervalMs = 500;
constexpr int kHeartbeatTimeoutMsSys = 200;    // SYS→RT liveness, automotive FTTI
constexpr int kHeartbeatTimeoutMsJetson = 500; // Jetson→RT, cmd staleness
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
| Command stale | Watchdog 500 ms | Zero `0x202` + stop `0x200` |
| Obstacle timeout | Echo > 30 ms | Distance = UINT32_MAX |
| Rear motor encoder missing | Speed = 0 | PID on stale measurement (I-term saturates) |
| Wheel encoder missing (any) | No pulses for >1s at known speed | Log warning; differential odometry degraded. Does NOT trigger ESTOP. |
| Steering CAN TX fail | TWAI TX errors | Log, EPS-C will timeout-fault |
| Steering following error | abs(cmd − actual) > 5° for 300 ms | `mode_set(Estop)` |
| Steering sync timeout | No `0x201` within 2s of boot | Log error, remain in MANUAL |
| Gateway queue full | `xQueueSend` fail | Drop (except 0x001 — direct TX) |

### 7.11 Startup

```
1. can_low_init() → TWAI, low CAN
2. can_high_init() → SPI + MCP2515, high CAN
3. obstacle_init() → TRIG/ECHO GPIOs
4. pid_init() → load gains
5. watchdog_init() → timestamp
6. steer SM → STEER_BOOT_WAIT (500ms) → STEER_LISTEN_SYNC (await 0x201) → STEER_ACTIVE
7. Create queues (6), Create 9 tasks
8. ESP_LOGI("Ready")
```

---

## 8. SYS ESP32-S3 — Safety, Motor Actuation & Body Control

### 8.1 Role

Safety (E-stop, brake lever, RT heartbeat), motor actuation (0–5V via MCP4725, 72V gear via relays), brake control (SYNTREE SEB via CAN `0x720`), DC-DC converter, signal lights, mode indicators, 12V power, diagnostics.

Low-level CAN only. Jetson communication via RT.

**15 FreeRTOS tasks** on ESP32-S3 @ 240 MHz, 1000 Hz tick.

### 8.2 CAN interface

Built-in TWAI, GPIO 4/5, 500 kbit/s, SN65HVD230.

### 8.3 CAN messages received

| ID | Name | Payload | Source | Action |
|----|------|---------|--------|--------|
| `0x001` | SAFETY_ESTOP | — | RT or any | `mode_set(Estop)` |
| `0x202` | RT_DRIVE_SETPOINT | `{i32 speed, u8 gear}` | RT | → `setpoint_queue` |
| `0x302` | HOST_LIGHT_CMD (fwd) | `u8` bitfield | RT | → `g_light_state` |
| `0x721` | SEB_STATUS | `{u8 status, u16 stroke, …}` (8 bytes) | SEB | Sync boot stroke, brake feedback |
| `0x7FF` | RT HEARTBEAT | `u8 alive_ctr` | RT | Feed RT alive counter |

### 8.4 CAN messages sent

| ID | Name | Payload | Rate | Notes |
|----|------|---------|------|-------|
| `0x011` | SYS_SAFETY_STATUS | `{u8 estop, u8 hb_ok}` | 5 Hz | → RT (fwd to Jetson) |
| `0x012` | SYS_DCDC_CMD | `u8 enable` | Change | → DC-DC converter |
| `0x110` | SYS_MODE_CMD | `u8 mode` | Change | → RT |
| `0x120` | SYS_THROTTLE_POS | `i16 speed_mmps` | 100 Hz | → RT (fwd to Jetson) |
| `0x600` | SYS_DIAG | 8 bytes | 1 Hz | → RT (fwd to Jetson) |
| `0x720` | VCU_SEB_REQ | `{u8 ctrl[2], u16 stroke, u16 press, u8 sec, u8 cksum}` (8 bytes) | **50 Hz** | → SYNTREE SEB |
| `0x7FF` | SYS HEARTBEAT | `u8 alive_ctr` | 2 Hz | → RT |

### 8.5 Internal data types

```cpp
enum class SysMode : uint8_t { Manual = 0, Auto = 1, Estop = 2 };
enum class Gear : uint8_t { N = 0, D = 1, S = 2, R = 3 };

enum class BrakeState : uint8_t {
    BRAKE_BOOT_WAIT,     // 500ms — do NOT transmit
    BRAKE_LISTEN_SYNC,   // Wait for 0x721 SEB_STATUS, read current stroke
    BRAKE_ACTIVE,        // Transmit 0x720 at 50 Hz
    BRAKE_FAULT
};

struct ActuatorSetpoint {
    int32_t motor_speed_mmps = 0;
    Gear    gear             = Gear::N;
};
```

### 8.6 Control mechanisms

#### Throttle, Gear, DC-DC — unchanged

See §8.6 of prior revision for: MCP4725 I2C DAC (0–5V), TLP281 optoisolator inputs + relay module outputs (72V) with 1A fuse + SMCJ90CA TVS protection, `0x012` DCDC CAN control.

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

**Mode-dependent behavior:**

| Mode | Turn signals | Headlight | Brake light |
|------|-------------|-----------|-------------|
| MANUAL | Rider switches (GPIOs TBD) | Rider switch (TBD) | **OR logic** — lever + Jetson bit |
| AUTO | `g_light_state` from CAN `0x302` | `g_light_state.headlight` | **OR logic** — lever + ESTOP + Jetson bit |
| ESTOP | OFF | OFF | **ON** (forced, overrides all) |

**Turn signal blink pattern** (`lights_task`):
- 500 ms ON, 500 ms OFF, repeating while `left_turn` or `right_turn` is true
- Canceled when both are false

#### Mode switch — push button toggle

A momentary push button on GPIO11 (active-low, internal pull-up, debounced). Each press toggles the mode: MANUAL → AUTO → MANUAL. ESTOP cannot be exited via the button — requires power-cycle or explicit CAN command.

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

`0x7FF` is a **1-byte alive counter** (not an empty frame). Every 500 ms each node increments its counter and broadcasts. A frozen counter = a frozen node, even if the CAN controller is still transmitting from a hardware buffer.

| Parameter | RT→SYS | SYS→RT | Jetson→RT |
|-----------|--------|--------|-----------|
| CAN ID | `0x7FF` | `0x7FF` | `0x7FF` |
| DLC | 1 | 1 | 1 |
| Payload | `u8 alive_ctr++` | `u8 alive_ctr++` | `u8 alive_ctr++` |
| Period | 500 ms (2 Hz) | 500 ms (2 Hz) | 500 ms (2 Hz) |
| Timeout | **200 ms** | **200 ms** | 500 ms |
| Missed frames to trigger | 1 (at 2 Hz, 200ms = ~1 missed) | 1 | 2 |
| Action on loss | SYS → ESTOP (AUTO only) | RT → CAN `0x001` ESTOP on low (AUTO only) | RT → zero `0x202` + stop `0x200` |

**Why 200 ms for inter-MCU?**

SYS→RT→SYS is the safety-critical spine. At 25 km/h the trike travels ~1.4 m in 200 ms — well within a controlled-stop envelope. 1500 ms would be 10 meters, which is unacceptable for steer-by-wire (ISO 26262 FTTI < 200 ms for ASIL-C steering).

**Startup grace period:**

At boot, heartbeats haven't been established. `safety_heartbeat_ok()` returns `true` for the first **3 seconds** if `last_hb_timestamp == 0`. After the grace period, real heartbeat checking begins. This prevents false ESTOP during boot.

```cpp
bool safety_heartbeat_ok() {
    int64_t now = esp_timer_get_time();
    if (last_hb_rt_us == 0) {
        return (now < kStartupGracePeriodUs);  // 3_000_000
    }
    return ((now - last_hb_rt_us) / 1000) < kHeartbeatTimeoutMs;  // 200
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
| RT | **21** | `safety_task` (or `control_task`) | 20 Hz / 100 Hz | TPS3850 or equiv, 100ms window |
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

#### Brake — SYNTREE SEB via CAN (`0x720`)

**Boot sequence — "Listen Before Speaking":**

```
BRAKE_BOOT_WAIT:
  - 500ms delay after power-on
  - DO NOT transmit any 0x720 frames
  - → BRAKE_LISTEN_SYNC

BRAKE_LISTEN_SYNC:
  - Wait for 0x721 SEB_STATUS frame
  - Extract SEB_Stroke_Value (u16, scale 0.05, offset -30)
  - Set initial command target = current stroke (hold position)
  - Wait for SEB_Alignment_Status == 1
  - → BRAKE_ACTIVE

BRAKE_ACTIVE:
  - Transmit 0x720 at 50 Hz continuously
  - Rolling counter increments 0→15 every frame
  - Checksum = XOR(bytes 0–6) ^ 0xFF (verify against spec)

BRAKE_FAULT:
  - Stop transmitting
```

**SYNTREE SEB protocol:**

| Parameter | Value |
|-----------|-------|
| Command ID | `0x720` |
| Rate | 50 Hz (20 ms) — continuous transmission required |
| Control mode | 1 = Stroke Mode, 2 = Pressure Mode |
| Stroke range | -5 to 27 mm (raw: 500–1140, scale 0.05, offset -30) |
| Rolling counter | 4-bit, increment every frame |
| Checksum | XOR of bytes 0–6, then `^ 0xFF` (verify against spec) |

> **Current mode: Stroke (1).** SYS only has two binary brake triggers — lever (pressed/released) and ESTOP (on/off). These map directly to stroke positions. **Pressure Mode (2) is planned for AUTO** once the brake arbitration gap (#12.1) is closed. At that point, RT sends an arbitrated brake target to SYS, and SYS commands the SEB in Pressure Mode — the SEB's internal PID maintains exact hydraulic pressure, compensating for pad wear and temperature.

**Mode-dependent behavior:**

| Mode | Brake behavior |
|------|---------------|
| MANUAL | Brake lever GPIO2 LOW → stroke = `kBrakeManualStroke` (~15 mm). Released → stroke = 0 mm. Transmit at 50 Hz. |
| AUTO | TBD — depends on RT brake arbitration gap resolution. Currently: no lever → stroke = 0. In future: RT-arbitrated target via `0x200` brake field or new ID. |
| ESTOP | Stroke = `kBrakeMaxStroke` (full brake, ~27 mm). Transmit at 50 Hz. |

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

Pri 4  dispatch    ◀── can_rx_queue: 0x200→setpoint, 0x302→light, 0x001→ESTOP, 0x721→brake_feedback
       mode        ── Push button (GPIO11) @ 10 Hz → toggle MANUAL↔AUTO, CAN 0x110
       motor       ◀── setpoint_queue (4, overwrite)
             100 Hz: AUTO→MCP4725+gear, MANUAL→pass-through, ESTOP→all off

Pri 3  throttle    ── ADC @ 100 Hz → CAN 0x120
       gear        ── Gear FSM @ 50 Hz
       brake       ── Brake FSM @ 50 Hz → CAN 0x720 (continuous, rolling ctr + cksum)
       lights      ── Light FSM @ 20 Hz (blink timing, ESTOP=brake ON)
       dcdc        ── DCDC FSM @ 5 Hz → CAN 0x012

Pri 2  indicator   ── Mode bulbs @ 5 Hz
       power       ── 12V relay @ 5 Hz
       can_tx      ── Safety status @ 5 Hz → CAN 0x011

Pri 1  diag        ── System health @ 1 Hz → CAN 0x600
       hb          ── 0x7FF @ 2 Hz
```

| Task | Prio | Stack | Period | Behavior |
|------|------|-------|--------|----------|
| `can_rx` | 5 | 4096 B | Event | `twai_receive()`, copy to queue |
| `safety` | 5 | 2048 B | 20 Hz | ESTOP GPIO, RT HB timeout |
| `dispatch` | 4 | 3072 B | Event | Route 0x200, 0x302, 0x001, 0x721 |
| `mode` | 4 | 2048 B | 10 Hz | Push button debounce + toggle MANUAL↔AUTO, CAN 0x110 |
| `motor` | 4 | 2048 B | 100 Hz | MCP4725 DAC + gear outputs |
| `throttle` | 3 | 1536 B | 100 Hz | ADC read, CAN 0x120 |
| `gear` | 3 | 1536 B | 50 Hz | TLP281 read / setpoint → relays |
| `brake` | 3 | 2048 B | **50 Hz** | Brake SM (boot sync) + CAN 0x720 with rolling ctr + checksum |
| `lights` | 3 | 1536 B | 20 Hz | Light GPIOs + blink |
| `dcdc` | 3 | 1024 B | 5 Hz | DCDC FSM, CAN 0x012 |
| `indicator` | 2 | 1024 B | 5 Hz | Mode bulbs (AUTO/MANUAL) |
| `power` | 2 | 1024 B | 5 Hz | 12V relay |
| `can_tx` | 2 | 3072 B | 5 Hz | CAN 0x011 |
| `diag` | 1 | 2048 B | 1 Hz | CAN 0x600 |
| `hb` | 1 | 2048 B | 2 Hz | CAN 0x7FF |

### 8.8 Hardware pin assignments

| Signal | GPIO | Direction | Conditioning |
|--------|------|-----------|-------------|
| CAN TX (low) | 5 | Out | SN65HVD230 |
| CAN RX (low) | 4 | In | SN65HVD230 |
| E-stop button | 1 | In | Big red mushroom, NC (active-low), pull-up, hardware ISR |
| Brake lever | 2 | In | Active-low, pull-up |
| Start button | **32** | In | Momentary, active-low, pull-up, debounced. Exits ESTOP → MANUAL. |
| Throttle read | 10 | In (ADC1_CH5) | Voltage divider 5V→3.3V |
| Throttle output | — | I2C (SDA=15, SCL=16) | MCP4725, addr 0x60, VCC=5V |
| Gear D sense | 12 | In | TLP281 optoisolator ch1 |
| Gear S sense | 13 | In | TLP281 optoisolator ch2 |
| Gear R sense | 14 | In | TLP281 optoisolator ch3 |
| Gear D output | 33 | Out | Relay ch1 → 1A fuse → ECU, TVS to GND |
| Gear S output | 34 | Out | Relay ch2 → 1A fuse → ECU, TVS to GND |
| Gear R output | 35 | Out | Relay ch3 → 1A fuse → ECU, TVS to GND |
| Mode button | 11 | In | Push button, active-low, pull-up, debounced (momentary toggle MANUAL↔AUTO) |
| Left turn | 18 | Out | Relay → lamp |
| Right turn | 19 | Out | Relay → lamp |
| Brake light | 21 | Out | Relay → lamp |
| Headlight | 22 | Out | Relay → lamp |
| AUTO bulb | 25 | Out | Relay → bulb (12V rail) |
| MANUAL bulb | 26 | Out | Relay → bulb (12V rail) |
| 12V relay | 27 | Out | Secondary cut on ESTOP |
| WDT toggle | **23** | Out | External watchdog IC (TPS3850). Toggled by `safety_task` every 50 ms. |

### 8.9 Configuration constants

```cpp
namespace sys {
// CAN
constexpr int kCanBitrateHz = 500000, kCanTxGpio = 5, kCanRxGpio = 4;
// Throttle
constexpr int kThrottleAdcChannel = 5;        // ADC1_CH5 → GPIO10
constexpr int kThrottleI2cSda = 15, kThrottleI2cScl = 16;
constexpr uint8_t kThrottleDacI2cAddr = 0x60; // MCP4725
constexpr unsigned kThrottleDeadZone = 200;
constexpr int kThrottleMaxSpeedMmps = 3000, kThrottleDacMaxVal = 4095;
// Gear
constexpr int kGearDSense = 12, kGearSSense = 13, kGearRSense = 14;
constexpr int kGearDOut = 33, kGearSOut = 34, kGearROut = 35;
// Safety
constexpr int kEstopGpio = 1, kBrakeLeverGpio = 2, kModeSwitchGpio = 11;
constexpr int kWdtToggleGpio = 23;
// Lights
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
constexpr int kHeartbeatIntervalMs = 500;
constexpr int kHeartbeatTimeoutMs = 200;       // automotive FTTI
constexpr int kStartupGracePeriodMs = 3000;    // mask at boot
constexpr int kSafetyCheckHz = 20, kGearCheckHz = 50;
// Brake (SYNTREE SEB)
constexpr int kBrakeCmdRateHz = 50, kBrakeBootWaitMs = 500;
constexpr float kBrakeManualStroke = 15.0f, kBrakeMaxStroke = 27.0f;
constexpr float kBrakeStrokeScale = 0.05f, kBrakeStrokeOffset = -30.0f;
constexpr int kBrakeCmdId = 0x720;
} // namespace sys
```

### 8.10 Error handling

| Failure | Detection | Response |
|---------|-----------|----------|
| E-stop pressed | GPIO1 LOW | ESTOP → DAC=0, gears off, brake=max, DCDC off, 12V off |
| CAN bus-off | TWAI TEC > 255 | Log, auto-recover |
| RT HB timeout | `0x7FF` alive ctr frozen >200ms | ESTOP (AUTO only) |
| Brake lever | GPIO2 LOW | Engage brake |
| ADC fail | `adc1_get_raw()==0` | Throttle = 0 |
| Gear sense conflict | Multiple lines HIGH | Treat as N (fail-safe) |
| DCDC CAN TX fail | TWAI TX errors | 12V relay backup cut |
| SEB sync timeout | No `0x721` within 2s of boot | `BRAKE_FAULT`, lever inop |
| SEB checksum fail | SEB rejects frame | Frame dropped; counter still increments |
| External WDT timeout | TPS3850 MR pin | MCU hardware reset → all outputs safe state |
| Queue full | `xQueueSend` fail | Frame dropped |

### 8.11 Startup

```
 1. can_driver_init()     → TWAI, low-level CAN
 2. mode_manager_init()   → GPIO11 (MODE), GPIO32 (START)
 3. safety_monitor_init() → GPIO1 (ESTOP), GPIO2 (brake lever), WDT GPIO23
 4. throttle_init()       → ADC1_CH5 + I2C + MCP4725 (output=0)
 5. gear_init()           → GPIO12-14 (IN), GPIO33-35 (OUT, LOW)
 6. lights_init()         → GPIO18-22,25-26 (OUT, LOW)
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

---

## 9. CAN bus device maps

### Low-level

```
 Low-Level CAN (500 kbit/s)
  ├── RT ESP32-S3 (TWAI)        TX: 0x200,0x202,0x302,0x001,0x7FF
  │                              RX: 0x001,0x011,0x110,0x120,0x201,0x600,0x7FF
  ├── SYS ESP32-S3               TX: 0x011,0x012,0x110,0x120,0x600,0x720,0x001,0x7FF
  │                              RX: 0x001,0x202,0x302,0x721,0x7FF
  ├── SYNTREE EPS-C (steering)   TX: 0x201 | RX: 0x200
  ├── SYNTREE SEB (brake)        TX: 0x721 | RX: 0x720
  └── DC-DC converter (72→12V)  RX: 0x012
```

### High-level

```
 High-Level CAN (500 kbit/s)
  ├── Jetson Orin NX             TX: 0x300,0x301,0x302,0x001,0x7FF
  │                              RX: 0x001,0x011,0x120,0x210,0x220,0x400,0x600,0x7FF
  └── RT ESP32-S3 (MCP2515)      TX: 0x011,0x120,0x210,0x220,0x400,0x600,0x001,0x7FF
                                  RX: 0x001,0x300,0x301,0x302,0x7FF
```

---

## 10. Hardware summary

| Node | Controller | MCU | Framework | CAN interfaces |
|------|-----------|-----|-----------|---------------|
| Jetson | Orin NX | — | ROS 2 | 1× CAN (high) |
| RT | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI (low) + MCP2515 SPI (high) |
| SYS | ESP32-S3 @ 240 MHz | Xtensa LX7 | ESP-IDF + FreeRTOS | TWAI (low only) |

| Parameter | Value |
|-----------|-------|
| CAN bitrate (both) | 500 kbit/s |
| CAN transceiver | SN65HVD230 |
| FreeRTOS tick | 1000 Hz |
| RT tasks | 9 |
| SYS tasks | 15 |

---

## 11. Build

```bash
cd rt-esp32 && pio run && pio run -t upload && pio device monitor
cd sys-esp32 && pio run && pio run -t upload && pio device monitor
```

---

## 12. Known design gaps

| # | Gap | Impact | Resolution |
|---|-----|--------|------------|
| 1 | RT brake arbitration (max-select of RT-computed + Jetson `0x301`) has no CAN path to SYS | Jetson `0x301` + RT obstacle braking never actuated. SYS brake via `0x720` uses ESTOP + lever only (Stroke Mode). AUTO braking — especially Pressure Mode for deceleration control — is blocked until resolved. | Add brake field to `0x202` RT_DRIVE_SETPOINT (DLC 5→6) or define `0x203 RT_BRAKE_CMD` (RT→SYS). |
| 2 | No CAN message for Jetson to request S (Sport) gear | AUTO can only select D/N/R | Add gear/sport field to `0x300` |
| 3 | Manual mode light switches not assigned GPIOs | Rider can't control signals/headlight in MANUAL | Assign GPIOs, read in `lights_task` |
| 4 | EPS-C timeout-fault behavior unknown | On ESTOP or comm loss, steering may lock, center, or freewheel | Verify with SYNTREE spec; implement appropriate mechanical safety |
| 5 | SEB pressure control mode not defined | SYS currently uses stroke mode only; pressure mode needed for brake arbitration | Define pressure target mapping from RT brake kPa to SEB MPa |

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
| [`notes/can-addressing-for-etrike.md`](notes/can-addressing-for-etrike.md) | CAN addressing scheme and bus load analysis |
| [`notes/can-troubleshooting.md`](notes/can-troubleshooting.md) | CAN debugging — common mistakes, error states, tools |
| [`notes/physics-model.md`](notes/physics-model.md) | Tricycle kinematics — forward/inverse, rollover, slip angles |
| [`notes/listen-before-speaking.md`](notes/listen-before-speaking.md) | CAN actuator safe bootstrapping pattern |
| [`notes/can-gateway-bridging.md`](notes/can-gateway-bridging.md) | CAN gateway forwarding rules and implementation |
| [`notes/defense-in-depth-safety.md`](notes/defense-in-depth-safety.md) | Layered safety — ESTOP, following error, dynamic clamp, OR logic |
| [`notes/syntree-security-protocol.md`](notes/syntree-security-protocol.md) | Rolling counter + XOR checksum for SYNTREE actuators |
| [`notes/high-voltage-isolation.md`](notes/high-voltage-isolation.md) | 72V galvanic isolation — TLP281 optos, relays, fuses, TVS |
| [`notes/distributed-architecture.md`](notes/distributed-architecture.md) | Three-node rationale — Jetson/RT/SYS split, dual-CAN on RT |
| [`notes/actuator-interfacing.md`](notes/actuator-interfacing.md) | MCP4725 DAC throttle, gear pass-through, relay logic |
| [`notes/external-watchdog.md`](notes/external-watchdog.md) | External watchdog IC — timeout, safe state, testing |
| [`notes/pid-speed-control.md`](notes/pid-speed-control.md) | PID speed control theory, tuning, anti-windup |
