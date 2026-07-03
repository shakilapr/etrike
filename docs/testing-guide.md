# Testing Guide — E-Trike Drive-by-Wire System

## Overview

The project has two parallel native (host-side) test systems that compile and run ECU logic on a PC without requiring ESP32 or STM32 hardware:

| System | Build tool | Location | What it tests |
|---|---|---|---|
| **CMake native-test** | CMake + GCC | `native-test/` | Full FreeRTOS integration tests with HAL stubs & virtual CAN |
| **PlatformIO native env** | PlatformIO (`pio run -e native`) | `rt-esp32/`, `sys-esp32/` | Lightweight header validation & core logic checks |

Both systems compile the same production source files. The difference is scope: CMake runs deep integration tests with a real FreeRTOS kernel; PlatformIO provides a fast (<2s) compilation gate.

---

## PlatformIO Native Environments

### What they are

`pio run -e native` compiles a subset of ECU source files using your PC's GCC/MinGW compiler instead of the ESP32 cross-compiler. This validates that core logic compiles without ESP-IDF dependencies.

### What they test

**RT native** (`rt-esp32`):
- `physics_model.cpp` — tricycle kinematics solver
- `native_entry.cpp` — validation harness

Checks performed:
- Struct sizes (`DriveCmd`, `ResolvedSetpoint`)
- Vehicle constants (wheelbase, speed limits, steer limit)
- Kinematics: straight-line resolution, gentle turn, dynamic steer limit
- Obstacle speed limiter: stop-distance and clear-distance behaviour

**SYS native** (`sys-esp32`):
- `mode_manager.cpp` — mode state machine (MANUAL/AUTO/ESTOP)
- `safety_monitor.cpp` — heartbeat watchdog, ESTOP/brake GPIO
- `native_entry.cpp` — validation harness

Checks performed:
- Mode transitions: MANUAL→AUTO→MANUAL toggle (release triggers falling_edge)
- ESTOP entry via `force_estop()`, ESTOP exit via START button release
- MODE button ignored during ESTOP
- SafetyMonitor: startup grace period, heartbeat OK/lost detection
- Heartbeat timeout: grace expires after 3s, normal timeout after 1s

### How to run

```bash
# RT native (from rt-esp32/)
cd rt-esp32
pio run -e native        # build only (~1.5s)
./.pio/build/native/program.exe   # run validation (~0.1s)

# SYS native (from sys-esp32/)
cd sys-esp32
pio run -e native
./.pio/build/native/program.exe

# Build + run in one line
pio run -e native && .pio/build/native/program.exe
```

Output shows `ok:` for each check passing, `FAIL:` for failures, and an overall `=== PASS ===` or `=== FAIL ===` at the end. Exit code is the failure count.

### How they work

The `[env:native]` section in `platformio.ini`:

```ini
[env:native]
platform = native              # Use host compiler, not ESP32 toolchain
build_src_filter = +<physics_model.cpp> +<native_entry.cpp>  # Only these files
build_flags =
    -mconsole                  # Windows: use console subsystem (not WinMain)
    -D HOST_BUILD              # Enable host-side code paths
    -I ../shared               # Access CAN protocol headers
    -I ../native-test/hal/shadow  # ESP-IDF stub headers (esp_log.h, etc.)
```

Key design:
- `build_src_filter` uses explicit positive patterns — only whitelisted files compile. New ESP32-specific files won't accidentally break the native build.
- `HOST_BUILD` guards the test `main()` in `native_entry.cpp` so the ESP32 build ignores it (ESP-IDF uses `app_main`, not `main`).
- Shadow HAL headers in `native-test/hal/shadow/` provide no-op stubs for `esp_log.h` (routes `ESP_LOGx` to `printf`). No FreeRTOS headers are needed — the compiled source files don't include them.
- The `TESTING` define (SYS only) makes `safety_monitor.cpp` use an injectable time variable (`g_sys_test_time_us`) instead of `esp_timer_get_time()`.

### Architecture

```
rt-esp32/src/native_entry.cpp     ── validation harness (#ifdef HOST_BUILD)
    ├── physics_model.h           ── kinematics pure header
    ├── config.h                  ── RT constants
    ├── shared_config.h           ── vehicle-wide constants
    └── can/can_protocol.h        ── CAN ID definitions

sys-esp32/src/native_entry.cpp    ── validation harness (#ifdef HOST_BUILD)
    ├── mode_manager.h/cpp        ── mode FSM
    ├── safety_monitor.h/cpp      ── heartbeat watchdog
    ├── config.h                  ── SYS constants
    └── shared_config.h           ── vehicle-wide constants

native-test/hal/shadow/
    └── esp_log.h                 ── ESP_LOGx → printf stubs
```

---

## CMake Native-Test Build

### What it is

The `native-test/CMakeLists.txt` build fetches the FreeRTOS kernel, builds HAL stubs for ESP32 peripherals (TWAI, GPIO, I2C, ADC, timers), creates a virtual CAN bus, and compiles test executables that exercise RT and SYS logic with real task scheduling.

### Prerequisites

```bash
# Windows: install TDM-GCC or MinGW-w64
# First-time configure:
cmake -S native-test -B native-test/build -G "MinGW Makefiles" \
    -DCMAKE_C_COMPILER=C:/TDM-GCC-64/bin/gcc.exe \
    -DCMAKE_CXX_COMPILER=C:/TDM-GCC-64/bin/g++.exe
```

### How to run

```bash
cmake --build native-test/build -j4
ctest --test-dir native-test/build --output-on-failure
```

Or build and run individual tests:

```bash
cd native-test/build
make smoke_test && ./smoke_test
make protocol_roundtrip && ./protocol_roundtrip
```

### Test inventory

| Test executable | Source | What it validates |
|---|---|---|
| `smoke_test` | `test/smoke_test.cpp` | FreeRTOS kernel + virtual CAN initialisation. Sends a CAN frame, verifies delivery. |
| `protocol_roundtrip` | `test/test_protocol_roundtrip.cpp` | All CAN message structs: `to_frame()` serialisation → `from_frame()` deserialisation roundtrips correctly for every signal. |
| `checksum_full` | `test/test_checksum_full.cpp` | SYNTREE security protocol checksums over all message types. |
| `mcp2515_config` | `test/test_mcp2515_config.cpp` | MCP2515 CNF register configuration constants — validates the bit-timing parameters compile to correct register values for 500 kbit/s. |
| `rt_can_rx_router` | `test/test_rt_can_rx_router.cpp` | RT CAN RX routing logic: frames from high-level bus are correctly forwarded to low-level bus based on ID routing table. |
| `rt_can_dispatch` | `test/test_rt_can_dispatch.cpp` | RT CAN dispatch: incoming 0x300 HostDriveCmd → physics model resolution → 0x204 RtDriveCmd output. Includes `physics_model.cpp` linked in. |
| `rt_safety_monitor` | `test/test_rt_safety_monitor.cpp` | RT safety monitor: heartbeat timeout detection, ESTOP propagation, command staleness watchdog. |
| `sys_can_dispatch` | `test/test_sys_can_dispatch.cpp` | SYS CAN dispatch: incoming 0x204/0x205 → atomic state updates → mode-dependent actuation decisions. |
| `task_watchdog` | `test/test_task_watchdog.cpp` | Multi-task watchdog: per-task alive counters, stall detection, deadline enforcement. |
| `test_safety_features` | `test/test_safety_features.cpp` | Cross-ECU safety: ESTOP broadcast (0x001), mode authority, brake arbitration, EGAS L2 monitoring. |
| `test_components` | `test/test_components.cpp` | Component-level integration: light control, DCDC control, indicator control, gear control state machines. |
| `test_component_io` | `test/test_component_io.cpp` | Component I/O: GPIO read/write through HAL stubs, verifies pin mappings match config constants. |
| `test_signal_chains` | `test/test_signal_chains.cpp` | End-to-end signal chains: Host 0x300 → RT physics → 0x204 → SYS motor DAC output. |
| `test_all_signals` | `test/test_all_signals.cpp` | Data-driven signal test: iterates over all CAN signals from generated data tables, verifies min/max/nominal values roundtrip correctly. |
| `test_gateway_forwarding` | `test/test_gateway_forwarding.cpp` | CAN gateway forwarding: RT high↔low bus bridge, PWT low↔powertrain bridge. |
| `test_dlc_consistency` | `test/test_dlc_consistency.cpp` | DLC consistency: verifies every message struct reports the correct DLC matching the CAN database. |

### How it works

**FreeRTOS**: Fetched via CMake `FetchContent` from `FreeRTOS/FreeRTOS-Kernel.git` at tag V11.1.0. Uses MSVC-MinGW port on Windows (native threads), POSIX port on Linux/macOS.

**HAL stubs** (`native-test/hal/`): Each stub implements the ESP-IDF HAL API with host-side behaviour:
- `twai_stubs.cpp` — virtual TWAI driver backed by `virtual_can_bus`
- `gpio_stubs.cpp` — GPIO level storage (set_level → array write, get_level → array read)
- `i2c_stubs.cpp` — I2C transaction buffer (records writes, replays reads)
- `adc_stubs.cpp` — ADC value injection for throttle simulation
- `esp_timer_stubs.cpp` — monotonic time with injectable offset

**Virtual CAN bus** (`native-test/can/virtual_can_bus.cpp`): A FreeRTOS queue-based CAN network. Multiple `CanDriver` instances connect to named buses ("high", "low", "powertrain"). Sending on one instance delivers to all others on the same bus. Enables multi-ECU integration tests on a single PC.

**Shadow headers** (`native-test/hal/shadow/`): Thin wrappers that sit before actual ESP-IDF headers in the include path. They use `#include_next` to chain to the real headers when available (ESP32 build) or provide standalone stubs when they're not (host build). Files include:
- `freertos/FreeRTOS.h`, `freertos/queue.h` — chain to real FreeRTOS
- `driver/twai.h`, `driver/gpio.h`, `driver/i2c.h`, `driver/adc.h`, `driver/spi_master.h` — ESP-IDF driver stubs
- `esp_log.h` — `ESP_LOGx` → `printf` with level prefix
- `esp_timer.h` — `esp_timer_get_time()` stub
- `esp_err.h`, `esp_heap_caps.h` — error code and heap stub types

---

## Test Logic & Design Patterns

### ModeManager tests (SYS)

The ModeManager is a push-button state machine with debounce. Key design details tested:

- **Falling-edge detection**: Buttons are active-low with pull-ups. `falling_edge(prev, now) = prev && !now` detects HIGH→LOW transitions (button release, not press). The state machine toggles on RELEASE, not press — this is the standard debounce pattern.
- **Debounce**: After any transition, a 500ms (5-tick at 10Hz) dead period ignores further edges. The test simulates this by calling `tick()` with `(false, false)` to advance the counter.
- **ESTOP behaviour**: MODE button is ignored in ESTOP (no toggle). START button exits ESTOP→MANUAL on release.
- **Long-press ESTOP exit** (gap #11): Holding MODE for 3s in ESTOP also exits to MANUAL. Test verifies this path doesn't fire on short presses.

### SafetyMonitor tests (SYS)

The SafetyMonitor validates RT heartbeat freshness:

- **Startup grace**: If no heartbeat has ever been received, the first 3 seconds (`kStartupGracePeriodMs`) are unconditionally OK. This prevents false ESTOP at boot before RT initialises.
- **Alive-counter validation**: Frozen counter (same value twice) = stuck CAN controller. The second identical counter is rejected — timestamp is NOT updated.
- **Normal timeout**: After grace expires, heartbeat older than `kHeartbeatTimeoutMsRt` (1000ms) triggers loss.
- **Time injection**: When `TESTING` is defined, `get_time_us()` reads `g_sys_test_time_us` (an `int64_t` global) instead of calling `esp_timer_get_time()`. The test sets this variable directly to simulate time passing without real delays.

### PhysicsModel tests (RT)

The tricycle kinematics model (`physics_model.cpp`) converts `(speed_mmps, yaw_rate_mrad_s)` → `(motor_speed_mmps, steer_angle_mdeg)`:

- **Straight line**: `(1000, 0)` → `motor_speed=1000, steer_angle=0`
- **Gentle turn**: `(2000, 200)` → positive steer angle (right turn)
- **Dynamic steer limit**: Angle limit decreases with speed (40° at 2km/h → 5° at 25km/h). Test verifies the slope.
- **Obstacle speed limiter**: Linearly scales speed from 0 at stop-distance (300mm) to full at clear-distance (3000mm).
- **Obstacle brake request**: Inverse of speed limiter — max brake pressure at stop-distance, zero at clear-distance.

### Protocol roundtrip tests

All CAN message structs (in `shared/can/can_protocol.h`) follow the pattern:
```cpp
struct Foo {
    int32_t bar;
    static Foo from_frame(const can::Frame& f);  // deserialise
    can::Frame to_frame() const;                   // serialise
};
```

The roundtrip test:
1. Creates a struct with known values
2. Calls `to_frame()` to serialise into a CAN frame
3. Calls `from_frame()` to deserialise back
4. Asserts the values match

This catches byte-ordering bugs, bit-packing errors, and signal range mismatches without needing CAN hardware.

---

## Build Environment Reference

| Environment | Define | Hardware assumed | Use case |
|---|---|---|---|
| `vehicle` | (none) | Full vehicle | Production firmware |
| `bench` | `BENCH_BUILD_ACKNOWLEDGED`, `CONFIG_BENCH_SOLO`, etc. | Single ECU on desk | Bench-top development |
| `native` | `HOST_BUILD` | PC only | Compilation gate + validation |

### Bypass flags (bench only)

| Flag | Effect |
|---|---|
| `CONFIG_BENCH_SOLO` | Skip peer ECU heartbeat timeouts |
| `CONFIG_BYPASS_EPS_C_SYNC` | Skip steering actuator listen-sync |
| `CONFIG_BYPASS_MTR_ABSENT` | Skip EGAS L2 speed monitoring |
| `TESTING` (SYS only) | Use injectable time instead of `esp_timer_get_time()` |
| `SYS_OWNS_MOTOR` (SYS bench) | SYS writes motor DAC directly (pending MTR migration) |

The `BENCH_BUILD_ACKNOWLEDGED` compile guard prevents accidental bench firmware deployment to a vehicle. Both RT and SYS `main.cpp` have:
```cpp
#if defined(CONFIG_BENCH_SOLO) && !defined(BENCH_BUILD_ACKNOWLEDGED)
#error "Bench build selected. Define BENCH_BUILD_ACKNOWLEDGED to proceed."
#endif
```
