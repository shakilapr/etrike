# E-Trike System Architecture

Three-node distributed control: **Jetson** (ROS 2 perception/planning), **RT ESP32-S3** (realtime physics, steering & CAN gateway), **SYS ESP32-S3** (safety, motor actuation & vehicle body control). Two physical CAN buses at 500 kbit/s.

- **Low-level CAN**: RT, SYS, brake module, steering module, DC-DC converter. Safety-critical actuation and inter-MCU commands.
- **High-level CAN**: Jetson, RT. Command, telemetry, and ROS 2 bridge traffic.

RT bridges the two buses — it is the only node connected to both.

## Node topology

```
  ┌─────────────────── High-Level CAN (500 kbit/s) ───────────────────┐
  │                                                                    │
  │  ┌──────────┐              ┌──────────────┐                       │
  │  │  Jetson  │              │  RT ESP32-S3 │                       │
  │  │  Orin NX │              │  (rt-esp32)  │                       │
  │  │          │              │              │                       │
  │  │ ROS 2    │              │ Physics      │                       │
  │  │ Planning │              │ Steering     │                       │
  │  │ Bridge   │              │ PID          │                       │
  │  └────┬─────┘              │ CAN Gateway  │                       │
  │       │                    └──────┬───────┘                       │
  │  TX: 0x300, 0x301,    TX: 0x011, 0x120, │                         │
  │       0x302, 0x001          0x210, 0x220,│                         │
  │                              0x400, 0x600,│                        │
  │  RX: 0x011, 0x120,          0x7FF, 0x001 │                         │
  │       0x210, 0x220,      RX: 0x300, 0x301,│                        │
  │       0x400, 0x600,          0x302, 0x001 │                         │
  │       0x7FF                              │                         │
  └──────────────────────────────────────────┘                         │
                                             │                         │
                 ┌───────────────────────────┘                         │
                 │                                                     │
  ┌──────────────▼──────── Low-Level CAN (500 kbit/s) ────────────────┐│
  │                                                                    ││
  │  ┌──────────────┐      ┌──────────────┐                            ││
  │  │  RT ESP32-S3 │      │ SYS ESP32-S3 │                            ││
  │  │  (rt-esp32)  │      │ (sys-esp32)  │                            ││
  │  │              │      │              │                            ││
  │  │ CAN Gateway  │      │ Safety       │                            ││
  │  └──────┬───────┘      │ Motor        │                            ││
  │         │              │ Brake        │                            ││
  │    TX: 0x200, 0x230,   └──────┬───────┘                            ││
  │         0x001, 0x7FF          │                                    ││
  │                          TX: 0x010, 0x011,                         ││
  │    RX: 0x001, 0x110,          0x012, 0x110,                        ││
  │         0x7FF                 0x120, 0x600,                        ││
  │                               0x001, 0x7FF                         ││
  │                          RX: 0x001, 0x200,                         ││
  │                               0x302, 0x7FF                         ││
  └────────────────────────────────────────────────────────────────────┘│
                                        │                               │
                       ┌────────────────┼────────────────┐              │
                       │                │                │              │
                 ┌─────▼─────┐   ┌─────▼─────┐   ┌─────▼─────┐        │
                 │  Brake    │   │ Steering   │   │   DC-DC   │        │
                 │  CAN Mod. │   │ CAN Mod.   │   │ Converter │        │
                 │           │   │(drive-by-  │   │ 72V→12V   │        │
                 │ (receives │   │ wire)      │   │(receives  │        │
                 │  0x010)   │   │(receives   │   │  0x012)   │        │
                 └───────────┘   │  0x230)    │   └───────────┘        │
                                 └────────────┘                        │
                                                                       │
  ┌───────────┐                                                       │
  │  Motor    │  (analog: 0–5 V throttle, 72 V gear lines from SYS)   │
  │Controller │                                                       │
  └───────────┘                                                       │
```

## Low-level CAN message catalog

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Priority |
|----|------|--------|-------------|-----|---------|--------|----------|
| `0x001` | SAFETY_ESTOP | Any | All (bridged by RT to high-level) | 0 | (none) | On event | Highest |
| `0x010` | SYS_BRAKE_CMD | SYS | Brake CAN module | 1 | u8 engage | On change | Very high |
| `0x011` | SYS_SAFETY_STATUS | SYS | RT (→ forwarded to Jetson) | 2 | u8 estop, u8 hb_ok | 5 Hz | Very high |
| `0x012` | SYS_DCDC_CMD | SYS | DC-DC converter (72V→12V) | 1 | u8 enable | On change | Very high |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | u8 mode (0=MANUAL, 1=AUTO) | On change | High |
| `0x120` | SYS_THROTTLE_POS | SYS | RT (→ forwarded to Jetson) | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x200` | RT_DRIVE_SETPOINT | RT | SYS | 5 | i32 speed_mmps, u8 gear (0=N, 1=D, 2=S, 3=R) | 100 Hz | Medium |
| `0x230` | RT_STEER_CMD | RT | Steering CAN module | 4 | i32 angle_mdeg | 100 Hz | Medium |
| `0x302` | HOST_LIGHT_CMD | RT (forwarded) | SYS | 1 | u8 lights (bitfield: b0=L, b1=R, b2=brake, b3=head) | On change | Medium |
| `0x600` | SYS_DIAG | SYS | RT (→ forwarded to Jetson) | 8 | diag struct | 1 Hz | Lowest |
| `0x7FF` | HEARTBEAT | RT, SYS | RT, SYS | 0 | (none) | 2 Hz | Lowest |

## High-level CAN message catalog

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Priority |
|----|------|--------|-------------|-----|---------|--------|----------|
| `0x001` | SAFETY_ESTOP | RT (forwarded) | Jetson | 0 | (none) | On event | Highest |
| `0x011` | SYS_SAFETY_STATUS | RT (forwarded) | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz | Very high |
| `0x120` | SYS_THROTTLE_POS | RT (forwarded) | Jetson | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x210` | RT_STATE_REPORT | RT | Jetson | 3 | u8 mode, u8 steer_valid, u8 reversing | 10 Hz | Low |
| `0x220` | RT_PID_FEEDBACK | RT | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | i32 speed_mmps, i32 yaw_rate_mrad_s | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | RT | 4 | i32 brake_pressure_kpa | On demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→ forwarded to SYS) | 1 | u8 lights (bitfield) | On change | Medium |
| `0x400` | RT_OBSTACLE_DIST | RT | Jetson | 4 | u32 distance_mm | 10 Hz | Low |
| `0x600` | SYS_DIAG | RT (forwarded) | Jetson | 8 | diag struct | 1 Hz | Lowest |
| `0x7FF` | HEARTBEAT | RT, Jetson | RT, Jetson | 0 | (none) | 2 Hz | Lowest |

## RT CAN gateway — forwarding rules

RT is the only node on both buses. It forwards messages transparently (same CAN ID, same payload):

| Direction | CAN IDs forwarded | Notes |
|-----------|-------------------|-------|
| Low → High | `0x011`, `0x120`, `0x600` | SYS telemetry → Jetson |
| Low → High | `0x001` | ESTOP from SYS → Jetson |
| High → Low | `0x302` | Jetson light commands → SYS |
| High → Low | `0x001` | ESTOP from Jetson → SYS + actuators |

**Not forwarded** (RT consumes/generates locally):
- `0x300`: Jetson → RT (consumed by physics model, not forwarded)
- `0x301`: Jetson → RT (consumed by brake arbitration, not forwarded)
- `0x200`, `0x230`: RT → SYS / steering (generated by RT, low-level only)
- `0x210`, `0x220`, `0x400`: RT → Jetson (generated by RT, high-level only)
- `0x010`, `0x012`, `0x110`: SYS → actuators/RT (low-level only)
- `0x7FF`: Independent heartbeats on each bus (not forwarded)

## Mode state machine

```
         ┌──────────┐
    ┌───▶│  MANUAL  │◀───┐
    │    └─────┬────┘    │
    │     switch=AUTO  switch=MANUAL
    │          │          │
    │    ┌─────▼────┐    │
    │    │   AUTO   │    │
    │    └─────┬────┘    │
    │          │          │
    │  ESTOP button / CAN 0x001 / HB timeout
    │          │          │
    │    ┌─────▼────┐    │
    └────│  ESTOP   │────┘  (cannot leave ESTOP via switch)
         └──────────┘
```

- **Manual**: Rider steers mechanically / rides throttle. SYS reads throttle ADC (0–5 V) and gear lines (72 V D/S/R), passes them through to motor controller. Brake lever read by SYS GPIO → CAN `0x010` → brake CAN module. Steering CAN module operates standalone (RT does not send `0x230` in MANUAL).
- **Auto**: Jetson sends `/cmd_vel` → high-level CAN `0x300` → RT resolves tricycle kinematics → RT sends speed+gear (`0x200`) and steering angle (`0x230`) on low-level CAN. SYS outputs 0–5 V throttle via DAC and energizes the appropriate 72 V gear line. Jetson controls signal lights via `0x302` (forwarded by RT).
- **Estop**: Motor stopped (0 V throttle, gear = N), brake engaged (CAN `0x010`), steering disabled, DC-DC converter off (CAN `0x012` enable=0). 12 V accessory power cut. Exit requires power-cycle or explicit CAN command (TBD).

## Signal flow per mode

### Manual mode

```
Throttle grip (0–5 V) ──► SYS ADC read ──► SYS DAC out (0–5 V) ──► Motor controller
Gear selector (72 V)  ──► SYS GPIO read ──► SYS relay out (72 V) ──► Motor controller
Brake lever            ──► SYS GPIO read ──► SYS CAN 0x010 ──► Brake CAN module
Steering wheel         ──► Steering CAN module (standalone, RT idle)
Signal lights          ──► Rider switches ──► SYS GPIO out ──► Light relays
DC-DC converter        ──► SYS CAN 0x012 enable=1 ──► DC-DC converter (72V→12V on)
```

### Auto mode

```
Jetson /cmd_vel ──► High-level CAN 0x300 ──► RT kinematics + PID
                                                │
                    ┌───────────────────────────┤
                    │                           │
                    ▼ (low-level CAN)           ▼ (low-level CAN)
       0x200 RT_DRIVE_SETPOINT        0x230 RT_STEER_CMD
       (speed_mmps, gear)             (angle_mdeg)
                    │                           │
                    ▼                           ▼
              SYS ESP32-S3              Steering CAN module
              │
              ├──► DAC → 0–5 V throttle → Motor controller
              ├──► Relay → 72 V gear line → Motor controller
              ├──► CAN 0x012 enable=1 → DC-DC converter
              └──► GPIO → Signal lights (from 0x302 forwarded by RT)

Jetson ──► High-level 0x301 ──► RT brake arbitration (max-select, TBD path to SYS)
Jetson ──► High-level 0x302 ──► RT forwards to low-level 0x302 ──► SYS → Light relays
SYS ────► Low-level 0x010 ──► Brake CAN module (on ESTOP or brake lever)
```

## Responsibility split

| Concern | Jetson | RT ESP32 | SYS ESP32 |
|---------|--------|----------|-----------|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| CAN gateway (low ↔ high) | | ✓ | |
| Tricycle kinematics | | ✓ | |
| Speed PID | | ✓ | |
| Steering angle compute | | ✓ | |
| Steering CAN TX (drive-by-wire) | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + button | | | ✓ |
| Brake CAN TX (to brake module) | | | ✓ |
| DC-DC converter CAN control (0x012) | | | ✓ |
| Heartbeat monitoring | | ✓ (Jetson, on high-level) | ✓ (RT, on low-level) |
| Mode switch reading | | | ✓ |
| Throttle ADC read (0–5 V, manual) | | | ✓ |
| Throttle DAC output (0–5 V, auto) | | | ✓ |
| Gear 72 V read (D/S/R, manual) | | | ✓ |
| Gear 72 V output (D/S/R, auto) | | | ✓ |
| Motor throttle DAC (0–5 V) + gear output (72 V) | | | ✓ |
| 12 V accessory power relay | | | ✓ |
| Auto / Manual mode indicator lights | | | ✓ |
| Signal lights (turn, brake, head) | | | ✓ |
| System diagnostics | | | ✓ |

## Design principles

1. **Tasks communicate through queues, never shared state.** No mutexes, no semaphores. Each queue is a thread-safe pipe.
2. **ESTOP bypasses the queue pipeline.** The safety task preempts everything and writes directly to actuators.
3. **One CAN ID = one sender per bus.** No duplicate IDs from different senders on the same bus (except heartbeat `0x7FF`). Forwarded messages keep the same ID on the other bus.
4. **Lower CAN ID = higher bus priority.** Safety IDs (`0x00X`) always win arbitration.
5. **All multi-byte CAN fields are big-endian (MSB first).**
6. **Manual mode is pass-through, not dead.** SYS reads physical inputs and mirrors them to outputs; the CAN bus is still live for telemetry but does not override the rider.
7. **Each vehicle subsystem (brake, steering, DC-DC) is a standalone CAN module on the low-level bus.** The ESP32 nodes command them; they do not bit-bang actuators directly.
8. **RT is the only dual-bus node.** It bridges safety and telemetry between low-level and high-level. No direct Jetson ↔ SYS path exists.

## Hardware

### Compute / MCU

| Parameter | Value |
|-----------|-------|
| Jetson | Orin NX |
| MCU (both) | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Framework | ESP-IDF with FreeRTOS (preemptive, tickless idle) |
| FreeRTOS tick | 1000 Hz (1 ms resolution) |
| CAN bitrate (both buses) | 500 kbit/s |

### RT ESP32-S3 — dual CAN interfaces

| Interface | Controller | Bus | GPIO | Notes |
|-----------|-----------|-----|------|-------|
| TWAI (built-in) | ESP32-S3 TWAI | **Low-level CAN** | TX=5, RX=4 | SN65HVD230 transceiver. Safety-critical RT ↔ SYS + actuators. |
| MCP2515 (external) | SPI | **High-level CAN** | SCK=36, MOSI=37, MISO=38, CS=39, INT=40 | SN65HVD230 transceiver. Jetson communication. |

### SYS ESP32-S3 peripherals

| Signal | GPIO | Direction | Conditioning | Notes |
|--------|------|-----------|-------------|-------|
| CAN TX (low-level) | 5 | Output | SN65HVD230 | TWAI |
| CAN RX (low-level) | 4 | Input | SN65HVD230 | TWAI |
| Throttle read (0–5 V) | 10 | Input (ADC1_CH5) | Voltage divider 5V→3.3V | Rider throttle grip |
| Throttle output (0–5 V) | — | I2C (SDA=15, SCL=16) | MCP4725 DAC (12-bit, VCC=5V → 0–5V out) | To motor controller |
| Gear D sense (72 V) | 12 | Input | TLP281 optoisolator (ch1) | Read gear selector, galvanic isolation |
| Gear S sense (72 V) | 13 | Input | TLP281 optoisolator (ch2) | |
| Gear R sense (72 V) | 14 | Input | TLP281 optoisolator (ch3) | |
| Gear D output (72 V) | 33 | Output | 4-ch 5V relay (ch1): GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS (SMCJ90CA) ECU wire→GND. | Drive motor controller |
| Gear S output (72 V) | 34 | Output | 4-ch 5V relay (ch2): GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS (SMCJ90CA) ECU wire→GND. | |
| Gear R output (72 V) | 35 | Output | 4-ch 5V relay (ch3): GPIO→IN, 72V→1A fuse→COM→NO→ECU. TVS (SMCJ90CA) ECU wire→GND. | |
| Mode switch | 11 | Input | Pull-up (Manual), GND (Auto) | |
| ESTOP button | 1 | Input (ISR) | Pull-up, debounced | Hardware interrupt |
| Brake lever | 2 | Input | Active-low, pull-up | |
| Mode light AUTO | 25 | Output | GPIO → LED | Indicator |
| Mode light MANUAL | 26 | Output | GPIO → LED | Indicator |
| Signal L turn | 18 | Output | GPIO → relay → lamp | |
| Signal R turn | 19 | Output | GPIO → relay → lamp | |
| Signal brake light | 21 | Output | GPIO → relay → lamp | |
| Signal headlight | 22 | Output | GPIO → relay → lamp | |
| 12 V accessory relay | 27 | Output | GPIO → relay → 12 V bus | Cut on ESTOP |

### RT ESP32-S3 peripherals (additional to CAN)

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| Ultrasonic TRIG | 7 | Output | HC-SR04 trigger |
| Ultrasonic ECHO | 8 | Input | HC-SR04 echo |
| Encoder A | 1 | Input | Speed feedback (PCNT) |
| Encoder B | 2 | Input | Speed feedback (PCNT) |
| I2C SDA | 10 | I/O | IMU (optional) |
| I2C SCL | 11 | Output | IMU (optional) |
| SPI SCK | 36 | Output | MCP2515 (high-level CAN) |
| SPI MOSI | 37 | Output | MCP2515 |
| SPI MISO | 38 | Input | MCP2515 |
| SPI CS | 39 | Output | MCP2515 chip select |
| MCP INT | 40 | Input | MCP2515 interrupt |

## CAN bus device maps

### Low-level CAN

```
 Low-Level CAN Bus (500 kbit/s)
  │
  ├── RT ESP32-S3 (built-in TWAI)    TX: 0x200, 0x230, 0x302(fwd), 0x001, 0x7FF
  │                                   RX: 0x001, 0x011, 0x110, 0x120, 0x600, 0x7FF
  ├── SYS ESP32-S3                    TX: 0x010, 0x011, 0x012, 0x110, 0x120, 0x600, 0x001, 0x7FF
  │                                   RX: 0x001, 0x200, 0x302(fwd), 0x7FF
  ├── Brake CAN module                (listens: 0x010)
  ├── Steering CAN module             (listens: 0x230)
  └── DC-DC converter (72V→12V)       (listens: 0x012)
```

### High-level CAN

```
 High-Level CAN Bus (500 kbit/s)
  │
  ├── Jetson Orin NX                  TX: 0x300, 0x301, 0x302, 0x001, 0x7FF
  │                                   RX: 0x001, 0x011, 0x120, 0x210, 0x220, 0x400, 0x600, 0x7FF
  └── RT ESP32-S3 (MCP2515 SPI)       TX: 0x011(fwd), 0x120(fwd), 0x210, 0x220, 0x400, 0x600(fwd), 0x001, 0x7FF
                                       RX: 0x001, 0x300, 0x301, 0x302, 0x7FF
```

## Known design gaps

| # | Gap | Impact | Resolution |
|---|-----|--------|------------|
| 1 | RT brake arbitration result (max-select of RT-computed + Jetson `0x301`) has no CAN path to SYS. `0x200` carries only speed + gear. | Jetson-requested braking and RT obstacle-emergency braking are computed but never actuated. | Add brake field to `0x200` (DLC 6) or define `0x201 RT_BRAKE_CMD` on low-level CAN. |
| 2 | No CAN message for Jetson to request S (Sport) gear mode. | AUTO mode can only select D/N/R. | Add gear/sport field to `0x300` or define separate message. |
| 3 | Manual mode signal light switches not yet assigned GPIOs on SYS. | Rider cannot control turn signals/headlight in MANUAL mode. | Assign GPIOs and read in `lights_task`. |
