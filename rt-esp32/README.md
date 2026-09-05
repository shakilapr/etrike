# RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

See [`architecture.md`](../architecture.md) for full system design and
[`docs/system-architecture-and-data-flow.md`](../docs/system-architecture-and-data-flow.md)
for the end-to-end ECU/host pipeline and both operating topologies.

## Pipeline context (Topology 1 — Autonomous)

RT is the **dual-bus gateway and autonomous motion master**. In autonomous mode the
vehicle pipeline is:

```
Jetson (High CAN 500 kbit/s)
  0x300 HOST_DRIVE_CMD  speed + curvature
  0x301 HOST_BRAKE_REQ  brake kPa
  0x302 HOST_LIGHT_CMD  lights
  0x7FC HOST_HEARTBEAT
        │
        ▼
RT ESP32-S3  (kinematics, obstacle clamp, steering safety, PID)
        │  Low CAN 500 kbit/s
        ├─► 0x169 VCU_SES_REQ  ─► SES  (steering)
        ├─► 0x204 RT_DRIVE_CMD ─► MTR  (motor speed + gear)
        └─► 0x7B9 VCU_SEB_REQ  ─► SEB  (brake)
```

RT is **silent in MANUAL** and only emits actuator commands in AUTO/ESTOP. When
Host/RT/SYS are absent, the **RM** node is the alternative raw-mode master that
drives the same low-bus actuators directly (see `rm-esp32/README.md`).

### What RT depends on from the other nodes
- **Host (Jetson):** `0x300`, `0x301`, `0x302`, `0x7FC` — RT enforces command
  staleness (< 200 ms) and host-heartbeat timeout (1500 ms) before acting.
- **SYS:** `0x7FE` SYS heartbeat (timeout → RT brake takeover) and `0x110` mode
  command; RT reads SYS `0x011` safety state for takeover detection.
- **SES / SEB:** status feedback `0x201`/`0x202` (SES) and `0x721`/`0x731` (SEB)
  with checksum-validated L3 fault checks.
- **MTR:** `0x206 MTR_MOTOR_FBK` (EGAS L2 feedback) and `0x120 SYS_THROTTLE_STS`.

## Build

```bash
cd rt-esp32
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial console
```

Target: `esp32-s3-devkitc-1-n16r8` (16 MB flash, 8 MB octal PSRAM) | Framework: `espidf` | FreeRTOS, 1000 Hz tick

## Host tests

```bash
cd test
g++ -std=c++17 -I. -I../src -I../../shared test_can_protocol.cpp -o test_can && ./test_can
g++ -std=c++17 -I. -I../../shared test_intermcu_protocol.cpp -o test_intermcu_protocol && ./test_intermcu_protocol
g++ -std=c++17 -I. -I../src -I../../shared test_pid.cpp ../src/speed_pid.cpp -o test_pid && ./test_pid
g++ -std=c++17 -I. -I../src -I../../shared test_physics.cpp ../src/physics_model.cpp -o test_physics && ./test_physics
g++ -std=c++17 -I. -I../src -I../../shared test_control_logic.cpp ../src/control_logic.cpp ../src/physics_model.cpp ../src/speed_pid.cpp -o test_control_logic && ./test_control_logic
g++ -std=c++17 -I. -I../src -I../../shared test_watchdog.cpp ../src/watchdog.cpp -o test_watchdog && ./test_watchdog
```
