# Hardware Integration Test Procedure

Step-by-step for the day hardware is connected. Each test must pass before
proceeding to the next. Stop on first failure — debug before continuing.

## Equipment Needed

- ESP32-S3 dev board (RT) with MCP2515 SPI module + WCMCU-230
- ESP32-S3 dev board (SYS) with WCMCU-230
- STM32F103 board (MTR) with CAN transceiver
- CANalyst-II USB analyzer (2 channels)
- USB-UART adapter for serial console (3x)
- 12V power supply
- Multimeter

## Phase 1 — Power-Up Check

### T1.1 — No smoke test
- Connect 12V to RT, SYS, MTR power inputs
- Verify no magic smoke, no hot components
- Measure 3.3V rail on each board: 3.3V ±0.1V

### T1.2 — Serial console
- Connect USB-UART to each board
- Open terminal (115200 baud)
- Power cycle each board
- Verify boot messages: "MCP2515 ready", "TWAI TX=5 RX=4 @ 500 kbit/s"

### T1.3 — CAN termination
- Power off
- Measure resistance between CANH and CANL on each bus
- Low bus: 60Ω (two 120Ω terminators in parallel)
- High bus: 60Ω
- If reading is 120Ω: one terminator missing. If 0Ω: short.

## Phase 2 — Single-Node CAN

### T2.1 — RT low bus TX
- Power RT only
- CANalyst-II Ch1 on low bus
- Verify 0x7FD RT_HEARTBEAT at 2 Hz (DLC=2, alive_ctr incrementing, health_flags present)
- Verify 0x7FE absent (SYS not powered)

### T2.2 — RT high bus TX (via MCP2515)
- CANalyst-II Ch1 on high bus
- Verify 0x7FD RT_HEARTBEAT at 2 Hz
- Verify 0x210 RT_STATE_RPT at 10 Hz (DLC=6, mode=0 Manual, safety_state=0)
- If nothing: check MCP2515 crystal (8 vs 16 MHz)

### T2.3 — SYS low bus TX
- Power SYS only
- CANalyst-II Ch1 on low bus
- Verify 0x7FE SYS_HEARTBEAT at 10 Hz
- Verify 0x011 SYS_SAFETY_STS at 5 Hz (DLC=3)

## Phase 3 — Dual-Node CAN

### T3.1 — Heartbeat cross-check
- Power RT + SYS
- CANalyst-II on low bus
- Verify both 0x7FD and 0x7FE present, independent counters
- On SYS serial: "RT alive" within 2s of boot

### T3.2 — Mode change
- Press MODE button on SYS (GPIO11 to GND momentarily)
- Verify 0x110 SYS_MODE_CMD on low bus (DLC=1, mode=1 Auto)
- Verify 0x210 RT_STATE_RPT mode byte changes from 0 to 1
- Press MODE again → mode returns to 0 (Manual)

### T3.3 — ESTOP
- Press ESTOP button (GPIO1 to GND)
- Verify 0x001 on low bus within 10ms
- Verify 0x001 on high bus within 10ms
- Verify 0x011 byte 0 = 1 (ESTOP active)
- Release ESTOP → press START (GPIO41 to GND)
- Verify mode returns to Manual

## Phase 4 — Autonomous Drive Path

### T4.1 — Host injection → RT forwarding
- Set mode to AUTO (MODE button)
- CANalyst-II Ch1: inject 0x300 on high bus (speed=1000, yaw=0, gear=D)
- CANalyst-II Ch2: verify on low bus:
  - 0x204 RT_DRIVE_CMD at 100 Hz (speed=1000, gear=D)
  - 0x169 VCU_SES_REQ at 50 Hz (DLC=8, checksum valid)

### T4.2 — Brake injection
- CANalyst-II Ch1: inject 0x301 on high bus (brake_kpa=2000)
- CANalyst-II Ch2: verify 0x205 RT_BRAKE_CMD at 50 Hz (kpa=2000)

### T4.3 — Obstacle braking
- CANalyst-II Ch1: inject 0x400 on high bus (distance=300mm)
- Verify 0x205 brake_kpa increases to 5000

### T4.4 — MANUAL mode silence
- Switch to MANUAL mode
- Verify 0x204, 0x205, 0x169 STOP on low bus
- Verify 0x210 safety_state still reported

## Phase 5 — ESTOP Latency

### T5.1 — CAN path latency
- Set mode to AUTO, inject 0x300 speed=1000
- Press ESTOP while logging both buses at 1ms resolution
- Verify: ESTOP press → 0x001 on bus < 10ms
- Verify: 0x204 speed→0 < 50ms after ESTOP

### T5.2 — Recovery
- After ESTOP, press START
- Verify mode returns to Manual
- Switch to AUTO
- Verify autonomous operation resumes

## Phase 6 — MTR Integration

### T6.1 — MTR boot
- Power MTR STM32
- Verify 0x120 SYS_THROTTLE_STS on low bus (DLC=2)
- Verify 0x206 MTR_MOTOR_FBK on low bus (DLC=4)
- Verify 0x206 byte 3 bit 4 = 1 (StartupReady)

### T6.2 — MTR follows mode
- SYS mode = MANUAL
- Verify MTR 0x206 gear_state = N
- SYS mode = AUTO
- Inject 0x204 speed=500, gear=D on low bus
- Verify MTR 0x120 speed_mmps = 500
- Verify MTR 0x206 gear_state = D

### T6.3 — MTR ESTOP
- With MTR in AUTO following 0x204
- Press ESTOP
- Verify MTR 0x206 byte 3 bit 0 = 1 (ESTOP_ACTIVE) within 100ms
- Verify MTR 0x120 speed_mmps = 0

## Pass Criteria

All tests T1.1 through T6.3 must pass before connecting steer-by-wire actuators
or the motor controller. Any failure: stop, debug, fix, restart from T1.1.
