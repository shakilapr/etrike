# RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

See [`architecture.md`](../architecture.md) for full system design.

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
