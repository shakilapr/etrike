# E-Trike System Architecture

Three-node distributed control: **Jetson** (ROS 2 perception/planning), **RT ESP32-S3** (realtime physics & steering), **SYS ESP32-S3** (safety & motor actuation). Communication over CAN bus at 500 kbit/s.

## Node topology

```
┌──────────────────────────────────────────────────────────────┐
│                        CAN Bus (500 kbit/s)                   │
│                                                               │
│  ┌──────────┐      ┌──────────────┐      ┌──────────────┐   │
│  │  Jetson  │      │  RT ESP32-S3 │      │ SYS ESP32-S3 │   │
│  │  Orin NX │      │  (rt-esp32)  │      │ (sys-esp32)  │   │
│  │          │      │              │      │              │   │
│  │ ROS 2    │      │ Physics      │      │ Safety       │   │
│  │ Planning │      │ Steering     │      │ Motor        │   │
│  │ Bridge   │      │ PID          │      │ Brake        │   │
│  └────┬─────┘      └──────┬───────┘      └──────┬───────┘   │
│       │                   │                     │            │
│  TX: 0x300, 0x001    TX: 0x200, 0x210,     TX: 0x010, 0x011,│
│                       0x220, 0x400, 0x7FF   0x120, 0x600, 0x7FF│
│  RX: 0x011, 0x210,   RX: 0x001, 0x110,     RX: 0x001, 0x200,│
│       0x220, 0x400,       0x300, 0x7FF          0x7FF        │
│       0x600, 0x7FF                                            │
└──────────────────────────────────────────────────────────────┘
```

## CAN message catalog

| ID | Name | Sender | Receiver(s) | DLC | Payload | Period | Priority |
|----|------|--------|-------------|-----|---------|--------|----------|
| `0x001` | SAFETY_ESTOP | Any | All | 0 | (none) | On event | Highest |
| `0x010` | SYS_BRAKE_CMD | SYS | (monitor) | 1 | u8 engage | On change | Very high |
| `0x011` | SYS_SAFETY_STATUS | SYS | Jetson | 2 | u8 estop, u8 hb_ok | 5 Hz | Very high |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | u8 mode (0/1) | On change | High |
| `0x120` | SYS_THROTTLE_POS | SYS | Jetson | 2 | i16 speed_mmps | 100 Hz | Medium |
| `0x200` | RT_DRIVE_SETPOINT | RT | SYS | 8 | i32 speed, i32 steer | 100 Hz | Medium |
| `0x210` | RT_STATE_REPORT | RT | Jetson | 3 | u8 mode, u8 steer_ok, u8 rev | 10 Hz | Low |
| `0x220` | RT_PID_FEEDBACK | RT | Jetson | 6 | i16 sp, i16 meas, i16 out | 10 Hz | Low |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | i32 speed, i32 yaw | ≤100 Hz | Medium |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | RT | 4 | i32 brake_pressure_kpa | On demand | Medium |
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

- **Manual**: Rider steers mechanically. Throttle via ADC. SYS drives motor from throttle.
- **Auto**: Jetson sends `/cmd_vel` → CAN `0x300` → RT resolves physics → SYS actuates motor. RT drives steering servo.
- **Estop**: Motor stopped, brake engaged, steering disabled. Exit requires power-cycle or explicit CAN command (TBD).

## Responsibility split

| Concern | Jetson | RT ESP32 | SYS ESP32 |
|---------|--------|----------|-----------|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| Tricycle kinematics | | ✓ | |
| Speed PID | | ✓ | |
| Steering servo (AUTO) | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| E-stop GPIO + button | | | ✓ |
| Brake lever + actuator | | | ✓ |
| Heartbeat monitoring | | | ✓ |
| Mode switch reading | | | ✓ |
| Motor PWM + direction | | | ✓ |
| Manual throttle ADC | | | ✓ |
| System diagnostics | | | ✓ |

## Design principles

1. **Tasks communicate through queues, never shared state.** No mutexes, no semaphores. Each queue is a thread-safe pipe.
2. **ESTOP bypasses the queue pipeline.** The safety task preempts everything and writes directly to actuators.
3. **One CAN ID = one sender.** No duplicate IDs on the bus (except heartbeat 0x7FF).
4. **Lower CAN ID = higher bus priority.** Safety IDs (0x00X) always win arbitration.
5. **All multi-byte CAN fields are big-endian (MSB first).**

## Hardware

| Parameter | Value |
|-----------|-------|
| MCU (both) | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Framework | ESP-IDF with FreeRTOS (preemptive, tickless idle) |
| FreeRTOS tick | 1000 Hz (1 ms resolution) |
| CAN controller | Built-in TWAI (ISO 11898-1 compatible) |
| CAN transceiver | External (e.g. SN65HVD230) |
| CAN bitrate | 500 kbit/s |
