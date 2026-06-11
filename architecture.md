# E-Trike System Architecture

Three-node distributed control: **Jetson** (ROS 2 perception/planning), **RT ESP32-S3** (realtime physics & steering), **SYS ESP32-S3** (safety, motor actuation & vehicle body control). Communication over CAN bus at 500 kbit/s.

## Node topology

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          CAN Bus (500 kbit/s)                             │
│                                                                           │
│  ┌──────────┐      ┌──────────────┐      ┌──────────────┐                │
│  │  Jetson  │      │  RT ESP32-S3 │      │ SYS ESP32-S3 │                │
│  │  Orin NX │      │  (rt-esp32)  │      │ (sys-esp32)  │                │
│  │          │      │              │      │              │                │
│  │ ROS 2    │      │ Physics      │      │ Safety       │                │
│  │ Planning │      │ Steering     │      │ Motor        │                │
│  │ Bridge   │      │ PID          │      │ Brake        │                │
│  └────┬─────┘      └──────┬───────┘      └──────┬───────┘                │
│       │                   │                     │                        │
│  TX: 0x300, 0x301,   TX: 0x200, 0x210,     TX: 0x010, 0x011,            │
│       0x302, 0x001         0x220, 0x230,         0x110, 0x120,           │
│                             0x400, 0x7FF          0x600, 0x7FF            │
│  RX: 0x011, 0x120,     RX: 0x001, 0x110,     RX: 0x001, 0x200,           │
│       0x210, 0x220,         0x300, 0x301,          0x302, 0x7FF           │
│       0x400, 0x600,         0x7FF                                         │
│       0x7FF                                                              │
└──────────────────────────────────────────────────────────────────────────┘
                                     │
                    ┌────────────────┼────────────────┐
                    │                │                │
              ┌─────▼─────┐   ┌─────▼─────┐   ┌─────▼─────┐
              │  Brake    │   │ Steering   │   │  Motor    │
              │  CAN Mod. │   │ CAN Mod.   │   │Controller │
              │           │   │(drive-by-  │   │(0-5V thr, │
              │ (receives │   │ wire)      │   │ 72V gear) │
              │  0x010)   │   │(receives   │   └───────────┘
              └───────────┘   │  0x230)    │
                              └────────────┘
```

## CAN message catalog

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Priority |
|----|------|--------|-------------|-----|---------|--------|----------|
| `0x001` | SAFETY_ESTOP | Any | All | 0 | (none) | On event | Highest |
| `0x010` | SYS_BRAKE_CMD | SYS | Brake CAN module | 1 | u8 engage | On change | Very high |
| `0x011` | SYS_SAFETY_STATUS | SYS | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz | Very high |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | u8 mode (0=MANUAL, 1=AUTO) | On change | High |
| `0x120` | SYS_THROTTLE_POS | SYS | Jetson | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x200` | RT_DRIVE_SETPOINT | RT | SYS | 5 | i32 speed_mmps, u8 gear (0=N, 1=D, 2=S, 3=R) | 100 Hz | Medium |
| `0x210` | RT_STATE_REPORT | RT | Jetson | 3 | u8 mode, u8 steer_valid, u8 reversing | 10 Hz | Low |
| `0x220` | RT_PID_FEEDBACK | RT | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz | Low |
| `0x230` | RT_STEER_CMD | RT | Steering CAN module | 4 | i32 angle_mdeg | 100 Hz | Medium |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | i32 speed_mmps, i32 yaw_rate_mrad_s | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | RT | 4 | i32 brake_pressure_kpa | On demand | Medium |
| `0x302` | HOST_LIGHT_CMD | Jetson | SYS | 1 | u8 lights (bitfield: b0=L, b1=R, b2=brake, b3=head) | On change | Medium |
| `0x400` | RT_OBSTACLE_DIST | RT | Jetson | 4 | u32 distance_mm | 10 Hz | Low |
| `0x600` | SYS_DIAG | SYS | Jetson | 8 | diag struct | 1 Hz | Lowest |
| `0x7FF` | HEARTBEAT | All | All | 0 | (none) | 2 Hz | Lowest |

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
- **Auto**: Jetson sends `/cmd_vel` → CAN `0x300` → RT resolves tricycle kinematics → RT sends speed+gear (`0x200`) to SYS and steering angle (`0x230`) to steering CAN module. SYS outputs 0–5 V throttle via DAC and energizes the appropriate 72 V gear line. Jetson controls signal lights via `0x302`.
- **Estop**: Motor stopped (0 V throttle, gear = N), brake engaged (CAN `0x010`), steering disabled. 12 V accessory power cut. Exit requires power-cycle or explicit CAN command (TBD).

## Signal flow per mode

### Manual mode

```
Throttle grip (0–5 V) ──► SYS ADC read ──► SYS DAC out (0–5 V) ──► Motor controller
Gear selector (72 V)  ──► SYS GPIO read ──► SYS relay out (72 V) ──► Motor controller
Brake lever            ──► SYS GPIO read ──► SYS CAN 0x010 ──► Brake CAN module
Steering wheel         ──► Steering CAN module (standalone, RT idle)
Signal lights          ──► Rider switches ──► SYS GPIO out ──► Light relays
```

### Auto mode

```
Jetson /cmd_vel ──► 0x300 HOST_DRIVE_CMD ──► RT kinematics + PID
                                                │
                    ┌───────────────────────────┤
                    │                           │
                    ▼                           ▼
       0x200 RT_DRIVE_SETPOINT        0x230 RT_STEER_CMD
       (speed_mmps, gear)             (angle_mdeg)
                    │                           │
                    ▼                           ▼
              SYS ESP32-S3              Steering CAN module
              │
              ├──► DAC → 0–5 V throttle → Motor controller
              ├──► Relay → 72 V gear line → Motor controller
              └──► GPIO → Signal lights (from 0x302 or autonomous)

Jetson ──► 0x301 HOST_BRAKE_REQUEST ──► RT (arbitrates, max-select) ──► (forwarded via 0x200, TBD)
SYS ────► 0x010 SYS_BRAKE_CMD ──► Brake CAN module  (on ESTOP or brake lever)
Jetson ──► 0x302 HOST_LIGHT_CMD ──► SYS → Light relays
```

## Responsibility split

| Concern | Jetson | RT ESP32 | SYS ESP32 |
|---------|--------|----------|-----------|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| Tricycle kinematics | | ✓ | |
| Speed PID | | ✓ | |
| Steering angle compute | | ✓ | |
| Steering CAN TX (drive-by-wire) | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + button | | | ✓ |
| Brake CAN TX (to brake module) | | | ✓ |
| Heartbeat monitoring | | | ✓ |
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
3. **One CAN ID = one sender.** No duplicate IDs on the bus (except heartbeat `0x7FF`).
4. **Lower CAN ID = higher bus priority.** Safety IDs (`0x00X`) always win arbitration.
5. **All multi-byte CAN fields are big-endian (MSB first).**
6. **Manual mode is pass-through, not dead.** SYS reads physical inputs and mirrors them to outputs; the CAN bus is still live for telemetry but does not override the rider.
7. **Each vehicle subsystem (brake, steering) is a standalone CAN module.** The ESP32 nodes command them; they do not bit-bang actuators directly.

## Hardware

### Compute / MCU

| Parameter | Value |
|-----------|-------|
| Jetson | Orin NX |
| MCU (both) | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Framework | ESP-IDF with FreeRTOS (preemptive, tickless idle) |
| FreeRTOS tick | 1000 Hz (1 ms resolution) |
| CAN controller | Built-in TWAI (ISO 11898-1 compatible) |
| CAN transceiver | External (e.g. SN65HVD230) |
| CAN bitrate | 500 kbit/s |

### SYS ESP32-S3 peripherals

| Signal | Direction | Conditioning | Notes |
|--------|-----------|--------------|-------|
| Throttle (0–5 V) | Input (ADC) | Voltage divider → 0–3.3 V | Read rider throttle in manual mode |
| Throttle (0–5 V) | Output (DAC / PWM+LPF) | Op-amp buffer / level shift → 0–5 V | Drive motor controller in auto mode |
| Gear D (72 V) | Input | Voltage divider 72 V → 3.3 V | Sense gear selector position |
| Gear S (72 V) | Input | Voltage divider 72 V → 3.3 V | |
| Gear R (72 V) | Input | Voltage divider 72 V → 3.3 V | |
| Gear D (72 V) | Output | GPIO → MOSFET/relay → 72 V | Mimic gear to motor controller |
| Gear S (72 V) | Output | GPIO → MOSFET/relay → 72 V | |
| Gear R (72 V) | Output | GPIO → MOSFET/relay → 72 V | |
| Mode switch | Input | Pull-up/-down | MANUAL / AUTO toggle |
| ESTOP button | Input (ISR) | Pull-up, debounced | Hardware interrupt |
| Mode light AUTO | Output | GPIO → LED / relay | Indicator |
| Mode light MANUAL | Output | GPIO → LED / relay | Indicator |
| Signal L turn | Output | GPIO → relay → lamp | |
| Signal R turn | Output | GPIO → relay → lamp | |
| Signal brake light | Output | GPIO → relay → lamp | |
| Signal headlight | Output | GPIO → relay → lamp | |
| 12 V accessory power | Output | GPIO → relay → 12 V bus | Cut on ESTOP |
| Motor throttle (0–5 V) | Output (DAC) | Op-amp buffer → 0–5 V → motor controller | |

### RT ESP32-S3 peripherals

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| CAN TX | 5 | Output | To SN65HVD230 TXD |
| CAN RX | 4 | Input | From SN65HVD230 RXD |
| Ultrasonic TRIG | 7 | Output | HC-SR04 trigger (10 µs pulse) |
| Ultrasonic ECHO | 8 | Input | HC-SR04 echo (pulse width → distance) |
| Encoder A | 1 | Input | Speed feedback (PCNT) |
| Encoder B | 2 | Input | Speed feedback (PCNT) |
| I2C SDA | 10 | I/O | IMU (optional, yaw-rate feedback) |
| I2C SCL | 11 | Output | IMU (optional) |

## CAN bus device map

```
 CAN Bus (500 kbit/s)
  │
  ├── Jetson Orin NX          (0x300, 0x301, 0x302, 0x001)
  ├── SYS ESP32-S3            (0x010, 0x011, 0x110, 0x120, 0x600, 0x001)
  ├── RT ESP32-S3             (0x200, 0x210, 0x220, 0x230, 0x400, 0x001)
  ├── Brake CAN module        (listens: 0x010)
  ├── Steering CAN module     (listens: 0x230)
  └── (future) BMS / display / logger
```

## Known design gaps

| # | Gap | Impact | Resolution |
|---|-----|--------|------------|
| 1 | RT brake arbitration result (max-select of RT-computed + Jetson `0x301`) has no CAN path to SYS. `0x200` carries only speed + gear. | Jetson-requested braking and RT obstacle-emergency braking are computed but never actuated. SYS only brakes on ESTOP or physical lever. | Add brake field to `0x200` (increase DLC to 6) or define new CAN ID `0x201 RT_BRAKE_CMD` (RT → SYS). |
| 2 | No CAN message for Jetson to request S (Sport) gear mode. `0x200` gear field supports it, but `0x300` has no corresponding field. | AUTO mode can only select D (forward), N (neutral), or R (reverse). Sport mode unreachable from Jetson. | Add gear/sport field to `0x300 HOST_DRIVE_CMD` or define separate message. |
| 3 | Manual mode signal light switches not yet assigned GPIOs. | Rider cannot control turn signals or headlight in MANUAL mode. | Assign GPIOs for physical light switches and read them in `lights_task`. |
