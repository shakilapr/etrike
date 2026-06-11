# ESP32-S3 — Unified Realtime Firmware

Single ESP32-S3 firmware for the 2-node E-Trike architecture (ESP32-S3 + Jetson Orin NX).

## Architecture

See [`achitecture.md`](../achitecture.md) for the system-level design.
See [`notes/rtos-architecture.md`](../notes/rtos-architecture.md) for the FreeRTOS task layout.

### 15 FreeRTOS tasks (2 cores)

| Task | Core | Prio | Rate |
|------|------|------|------|
| `safety` | 0 | 5 | 20 Hz |
| `can_rx0` | 0 | 5 | event |
| `can_rx1` | 0 | 5 | event |
| `dispatch` | 0 | 4 | event |
| `mode` | 0 | 4 | 10 Hz |
| `can_tx` | 0 | 4 | 5 Hz |
| `heartbeat` | 0 | 1 | 2 Hz |
| `watchdog` | 0 | 1 | 10 Hz |
| `control` | 1 | 4 | 100 Hz |
| `motor` | 1 | 4 | 100 Hz |
| `syntree_tx` | 1 | 4 | 50 Hz |
| `throttle` | 1 | 3 | 100 Hz |
| `brake` | 1 | 3 | 20 Hz |
| `obstacle` | 1 | 2 | 10 Hz |
| `diag` | 1 | 1 | 1 Hz |

## Build

```bash
cd rt-esp32
pio run              # build
pio run -t upload    # flash to ESP32-S3
pio device monitor   # serial console
```

## Host tests

```bash
cd test
g++ -std=c++17 -I. -I../src test_can_protocol.cpp -o test_can && ./test_can
g++ -std=c++17 -I. -I../src test_pid.cpp ../src/speed_pid.cpp -o test_pid && ./test_pid
g++ -std=c++17 -I. -I../src test_physics.cpp ../src/physics_model.cpp -o test_physics && ./test_physics
g++ -std=c++17 -I. -I../src test_control_logic.cpp ../src/control_logic.cpp ../src/physics_model.cpp ../src/speed_pid.cpp -o test_control_logic && ./test_control_logic
g++ -std=c++17 -I. -I../src test_watchdog.cpp ../src/watchdog.cpp -o test_watchdog && ./test_watchdog
g++ -std=c++17 -I. -I../src test_mode.cpp ../src/mode_manager.cpp -o test_mode && ./test_mode
g++ -std=c++17 -I. -I../src test_safety.cpp ../src/safety_monitor.cpp -o test_safety && ./test_safety
g++ -std=c++17 -I. -I../src test_can_rx_router.cpp ../src/can_rx_router.cpp -o test_can_rx_router && ./test_can_rx_router
g++ -std=c++17 -I. -I../src test_speed_limiter.cpp ../src/speed_limiter.cpp -o test_speed_limiter && ./test_speed_limiter
g++ -std=c++17 -I. -I../src test_motor_driver.cpp ../src/motor_driver.cpp -o test_motor_driver && ./test_motor_driver
```

| Parameter | Value |
|-----------|-------|
| Target | `esp32-s3-devkitc-1` |
| MCU | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz |
| Framework | `espidf` (ESP-IDF with FreeRTOS) |
| FreeRTOS tick | 1000 Hz |
| Public CAN | TWAI0, 500 kbit/s |
| Private CAN | TWAI1, 500 kbit/s (Syntree actuators) |
| CAN transceiver | SN65HVD230 (external) |
