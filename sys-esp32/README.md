# SYS ESP32-S3 — Safety, Motor Actuation & Body Control

See [`architecture.md`](../architecture.md) for full system design.

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
