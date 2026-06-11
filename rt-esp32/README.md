# RT ESP32-S3 — Realtime Physics, Steering & CAN Gateway

Dual-bus node (only node on both CAN buses):
- **High-level CAN** (MCP2515 SPI): communicates with Jetson — receives `/cmd_vel`, sends telemetry
- **Low-level CAN** (built-in TWAI): communicates with SYS + steering/brake/DCDC modules — sends setpoints, bridges messages

Converts ROS 2 `/cmd_vel`-style motion commands into:
- **Speed + gear** → CAN `0x200` → SYS ESP32-S3 (low-level)
- **Steering angle** → CAN `0x230` → drive-by-wire steering module (low-level)

## Architecture

See [`architecture-rt.md`](../architecture-rt.md) for full details.

### 9 FreeRTOS tasks

| Task | Prio | Rate |
|------|------|------|
| `can_rx_low` | 5 | Event-driven (TWAI) |
| `can_rx_high` | 5 | Event-driven (MCP2515 SPI) |
| `dispatch` | 4 | Event-driven |
| `control` | 4 | 100 Hz fixed |
| `can_tx_low` | 3 | Event-driven |
| `can_tx_high` | 3 | Event-driven |
| `obstacle` | 2 | 10 Hz |
| `watchdog` | 1 | 10 Hz |
| `heartbeat` | 1 | 2 Hz (both buses) |

## Build

```bash
cd rt-esp32
pio run              # build
pio run -t upload    # flash
pio device monitor   # serial console
```

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
