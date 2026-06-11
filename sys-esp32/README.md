# SYS ESP32-S3 — Safety, Motor Actuation & Body Control

Owns E-stop monitoring, brake CAN actuation, bidirectional throttle (0–5 V ADC/DAC), bidirectional gear selection (72 V D/S/R), mode switching, heartbeat watchdog, signal lights, mode indicator lights, 12 V accessory power, and system diagnostics.

## Architecture

See [`architecture-sys.md`](../architecture-sys.md) for full details.

## 14 FreeRTOS tasks

| Task | Prio | Rate |
|------|------|------|
| `can_rx` | 5 | Event-driven |
| `safety` | 5 | 20 Hz fixed |
| `dispatch` | 4 | Event-driven |
| `mode` | 4 | 10 Hz |
| `motor` | 4 | 100 Hz fixed |
| `throttle` | 3 | 100 Hz fixed |
| `gear` | 3 | 50 Hz fixed |
| `brake` | 3 | 20 Hz fixed |
| `lights` | 3 | 20 Hz fixed |
| `indicator` | 2 | 5 Hz |
| `power` | 2 | 5 Hz |
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
