# SYS ESP32-S3 — Safety, Motor Actuation & Body Control

See [`architecture.md`](../architecture.md) for full system design.

## Build

```bash
cd sys-esp32
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial console
```

Target: `esp32-s3-devkitc-1` | Framework: `espidf` | FreeRTOS, 1000 Hz tick

## Host tests

```bash
cd test
g++ -std=c++17 -I. -I../src -I../../shared test_mode.cpp ../src/mode_manager.cpp -o test_mode && ./test_mode
g++ -std=c++17 -I. -I../src -I../../shared test_safety.cpp ../src/safety_monitor.cpp -o test_safety && ./test_safety
g++ -std=c++17 -I. -I../src -I../../shared test_can_rx_router.cpp ../src/can_rx_router.cpp -o test_can_rx_router && ./test_can_rx_router
g++ -std=c++17 -I. -I../src -I../../shared test_speed_limiter.cpp ../src/speed_limiter.cpp -o test_speed_limiter && ./test_speed_limiter
g++ -std=c++17 -I. -I../src -I../../shared test_motor_driver.cpp ../src/motor_driver.cpp -o test_motor_driver && ./test_motor_driver
```
