# SYS ESP32-S3 — Safety & Motor Actuation

Owns E-stop monitoring, brake control, motor PWM, manual throttle ADC, mode switching, heartbeat watchdog, and system diagnostics.

## Architecture

See [`achitecture-sys.md`](../achitecture-sys.md) for full details.

## 10 FreeRTOS tasks

| Task | Prio | Rate |
|------|------|------|
| `can_rx` | 5 | Event-driven |
| `safety` | 5 | 20 Hz fixed |
| `dispatch` | 4 | Event-driven |
| `mode` | 4 | 10 Hz |
| `motor` | 4 | 100 Hz fixed |
| `throttle` | 3 | 100 Hz fixed |
| `brake` | 3 | 20 Hz fixed |
| `can_tx` | 2 | 5 Hz fixed |
| `diag` | 1 | 1 Hz |
| `hb` | 1 | 2 Hz |

## Build

```bash
cd sys-esp32
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial console
```

## Host tests

```bash
cd test
g++ -std=c++17 -I. -I../src -I../../shared test_mode.cpp ../src/mode_manager.cpp -o test_mode && ./test_mode
g++ -std=c++17 -I. -I../src -I../../shared test_safety.cpp ../src/safety_monitor.cpp -o test_safety && ./test_safety
g++ -std=c++17 -I. -I../src -I../../shared test_can_rx_router.cpp ../src/can_rx_router.cpp -o test_can_rx_router && ./test_can_rx_router
g++ -std=c++17 -I. -I../src -I../../shared test_speed_limiter.cpp ../src/speed_limiter.cpp -o test_speed_limiter && ./test_speed_limiter
g++ -std=c++17 -I. -I../src -I../../shared test_motor_driver.cpp ../src/motor_driver.cpp -o test_motor_driver && ./test_motor_driver
```
