# SYS ESP32-S3 — Safety, Mode Authority & Body Control

See [`architecture.md`](../architecture.md) for full system design and
[`docs/system-architecture-and-data-flow.md`](../docs/system-architecture-and-data-flow.md)
for the end-to-end ECU/host pipeline and both operating topologies.

> **Note on role:** SYS is the safety and mode authority, body controller, and
> EGAS L2 monitor. Direct motor I/O (throttle ADC/DAC/gear) is retired on SYS;
> the motor actuator is `mtr-stm32` (currently hardware-incomplete).

## Pipeline context

SYS is the **safety and mode authority**. It owns the vehicle mode state machine
(`MANUAL ↔ AUTO`, ESTOP overlaid on both) and broadcasts `0x110 SYS_MODE_CMD`
that every other node follows. It does not solve kinematics or command the motor
directly — it supervises and takes over when upstream nodes fail.

```
RT 0x7FD heartbeat ──┐
MTR 0x206 feedback  ──┼─► SYS safety monitor
Physical ESTOP/mode/brake GPIO ──┘
        │
        ▼
SYS ── 0x7B9 VCU_SEB_REQ ─► SEB   (brake, in MANUAL/ESTOP only)
SYS ── 0x110 SYS_MODE_CMD ─► ALL   (mode authority)
SYS ── 0x011 SYS_SAFETY_STS ─► RT/Jetson
SYS ── 0x7FE SYS_HEARTBEAT  ─► RT/MTR
```

- **AUTO mode:** SYS suppresses its own `0x7B9` so RT is the sole brake/steer
  master. If RT `0x7FD` is lost or RT safety state is degraded, SYS resumes
  sending `0x7B9` and trips the 72 V contactor (EGAS L2).
- **MANUAL/ESTOP:** SYS commands the SEB brake directly from the physical brake
  lever and its own safety logic.

### What SYS depends on from the other nodes
- **Physical inputs:** wired ESTOP button, mode button, start button, brake lever
  (SYS is the only node with these).
- **RT:** `0x7FD` heartbeat, `0x204`/`0x205` setpoints (EGAS L2 compare vs MTR
  `0x206`), `0x210` safety state, `0x7FE` consumed by RT.
- **MTR:** `0x206 MTR_MOTOR_FBK` — SYS reads `ESTOP_ACTIVE` bit for redundant ESTOP
  confirmation and speed-mismatch monitoring.
- **RM:** sends `0x111 HMI_MODE_REQ` / `0x112 HMI_PWR_REQ` *requests*; SYS is the
  sole decision maker and may ignore them in ESTOP. When Host/RT/SYS are offline,
  RM bypasses SYS entirely (see `rm-esp32/README.md`).

## Build

```bash
cd sys-esp32
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial console
```

Target: `esp32-s3-devkitc-1-n16r8` (16 MB flash, 8 MB octal PSRAM) | Framework: `espidf` | FreeRTOS, 1000 Hz tick

## Host tests

```bash
cd sys-esp32/test

# Brake control priority, auto_brake bit, kPa→raw conversion, rolling counter
g++ -std=c++17 -DTESTING -I. -I../src -I../../shared -I../../shared/can \
    test_brake_priority.cpp -o test_bp && ./test_bp

# Mode manager: MANUAL↔AUTO toggle, ESTOP exit, long-press, debounce
g++ -std=c++17 -DTESTING -I. -I../src -I../../shared -I../../shared/can \
    test_mode_manager.cpp ../src/mode_manager.cpp -o test_mm && ./test_mm

# Safety monitor: heartbeat timeout, frozen counter, startup grace
g++ -std=c++17 -DTESTING -I. -I../src -I../../shared -I../../shared/can \
    test_safety_monitor.cpp ../src/safety_monitor.cpp -o test_sm && ./test_sm
```
