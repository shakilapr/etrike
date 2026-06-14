# IntelliSense & Code Issues — Root Causes and Fixes

## Summary

All IntelliSense errors fall into **four root-cause categories**:

| # | Category | Files affected | Root cause |
|---|----------|---------------|------------|
| A | Missing `c_cpp_properties.json` | All `.cpp`/`.h` files in both projects | VS Code doesn't know about `-I ../shared`, ESP-IDF paths, or FreeRTOS headers |
| B | Orphaned `.cpp` files | `watchdog.cpp`, `diagnostics.cpp` | Implementation belongs to a different class than the header |
| C | Stub/hollow implementations | `throttle_input.h`, `mcp4725_dac.h`, `mode_manager.cpp`, `safety_monitor.cpp` | GPIO/I2C/ADC reads commented out; test harness needed |
| D | Architecture–implementation drift | `can_protocol.h`, `main.cpp` (both), `config.h` (both) | Minor mismatches in data types, missing fields, duplicated constants |

---

## A. Missing VS Code C/C++ Configuration (ALL IntelliSense errors)

### Problem

No `.vscode/c_cpp_properties.json` exists. VS Code's C/C++ language server sees:

```cpp
#include "freertos/FreeRTOS.h"   // ❌ not found
#include "driver/twai.h"          // ❌ not found
#include "can/can_protocol.h"     // ❌ not found (shared/ not in includePath)
```

This cascades: `QueueHandle_t` undefined → `xQueueCreate` undefined → `vTaskDelayUntil` undefined → every FreeRTOS type name fails.

### Fix

Create `.vscode/c_cpp_properties.json` with both ESP-IDF and shared include paths:

```json
{
    "configurations": [
        {
            "name": "ESP32-S3",
            "includePath": [
                "${workspaceFolder}/shared",
                "${workspaceFolder}/rt-esp32/src",
                "${workspaceFolder}/sys-esp32/src",
                "${config:esp_idf_path}/components/freertos/include",
                "${config:esp_idf_path}/components/freertos/FreeRTOS-Kernel/include",
                "${config:esp_idf_path}/components/driver/include",
                "${config:esp_idf_path}/components/hal/include",
                "${config:esp_idf_path}/components/esp_common/include",
                "${config:esp_idf_path}/components/log/include",
                "${config:esp_idf_path}/components/soc/include",
                "${config:esp_idf_path}/components/esp_hw_support/include",
                "${config:esp_idf_path}/components/esp_system/include",
                "${config:esp_idf_path}/components/newlib/platform_include"
            ],
            "defines": [
                "CONFIG_FREERTOS_HZ=1000",
                "CONFIG_TWAI_ISR_IN_IRAM=1",
                "CONFIG_IDF_TARGET_ESP32S3"
            ],
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "linux-gcc-arm",
            "compilerPath": "${config:esp_idf_tools_path}/tools/xtensa-esp32s3-elf/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-g++"
        }
    ],
    "version": 4
}
```

**Better alternative**: Install the [ESP-IDF VS Code extension](https://github.com/espressif/vscode-esp-idf-extension) and run `ESP-IDF: Add vscode configuration folder`. It auto-generates `c_cpp_properties.json` from the ESP-IDF environment.

**Even simpler**: Open each project folder (`rt-esp32/` or `sys-esp32/`) as a PlatformIO project in VS Code. The PlatformIO IDE extension auto-configures IntelliSense from `platformio.ini` — including the `-I ../shared` flag.

---

## B. Orphaned Implementation Files

### B1. `rt-esp32/src/watchdog.cpp` — belongs to wrong class

| File | Class defined |
|------|--------------|
| `watchdog.h` | `rt::CmdWatchdog` (used by `main.cpp`) |
| `watchdog.cpp` | `rt::Watchdog` (never declared in any header, never used) |

`watchdog.h` implements everything inline:

```cpp
class CmdWatchdog {
public:
    void init() { m_last_feed=-kCmdStaleTimeoutMs*1000; }
    void feed(int64_t now_us) { m_last_feed=now_us; }
    bool is_stale(int64_t now_us) const { ... }
};
```

But `watchdog.cpp` implements a completely different `rt::Watchdog` class with:
- `Watchdog::init()` — calls `esp_timer_get_time()` (not in header)
- `Watchdog::feed()` — uses `m_tripped` flag (not in `CmdWatchdog`)
- `Watchdog::is_stale()` — different signature (no parameter)

**Fix**: Delete `watchdog.cpp`. It's dead code. `CmdWatchdog` is fully header-only.

### B2. `sys-esp32/src/diagnostics.cpp` — uses wrong type name

```cpp
// diagnostics.cpp line 16
can::SysDiag diag;  // ❌ no such type exists
```

The shared header `can_protocol.h` defines `can::SysDiagRpt`, not `can::SysDiag`.

**But**: `main.cpp` also manually builds `can::SysDiagRpt` directly (lines 347-356) and calls `g_diag.report(...)` which is just logging. The `diagnostics.cpp` struct is unused by `main.cpp` and would fail to compile.

**Fix**: Either:
a) Fix `diagnostics.cpp` to use `can::SysDiagRpt`, and have `main.cpp` call `g_diag.report()` instead of building the frame itself, or
b) Delete `diagnostics.cpp` and make `Diagnostics` fully header-only (the report() function in main.cpp already does the work inline)

**Recommended**: Option (b) — delete `diagnostics.cpp`, move the `esp_get_free_heap_size()` call into the header or keep `main.cpp` building the frame itself (it already does).

---

## C. Stub / Hollow Implementations

These are intentional scaffolding — not bugs, but noted because they surface as IntelliSense noise.

### C1. `ThrottleInput::poll()` — empty body

```cpp
// throttle_input.h:10
void poll() { m_speed_mmps = 0; }  // ESP-IDF: adc1_get_raw(ADC1_CH5), map, store
```

ADC reads are commented out with TODO markers. The `tick()` method is also unused — `main.cpp` calls `poll()` directly instead of `tick(raw_adc)`.

**Impact**: Only `motor_driver.h` uses `ThrottleInput::tick()`, but `main.cpp` task_motor bypasses `MotorDriver` entirely and calls `g_throttle.poll()` / `g_dac.set_speed_mmps()` directly.

**Fix**: Either:
a) Wire `task_motor` to use `g_motor.tick(mode, setpoint)` consistently (the MotorDriver class), or
b) Delete `MotorDriver` class — it's dead wrapper code if main.cpp bypasses it

### C2. `Mcp4725Dac` — no I2C communication

```cpp
// mcp4725_dac.h
void init() { m_value = 0; }                    // no I2C init
void write(uint16_t val) { m_value = (val > 4095) ? 4095 : val; }  // no I2C write
```

Missing: `#include "driver/i2c.h"`, I2C master init, and actual `i2c_master_write_to_device()` calls.

**Fix**: This is a known gap — I2C hardware integration pending. Add `// TODO: I2C` markers.

### C3. `IndicatorControl` — missing `init()` method

```cpp
// indicator_control.h — no init() method
class IndicatorControl {
public:
    IndicatorOutputs tick(can::Mode mode) { ... }
};
```

But `main.cpp:406` calls `g_indicator.init()` — **compile error**.

**Fix**: Add `void init() {}` to `IndicatorControl`.

### C4. GPIO reads all commented out

Throughout `sys-esp32/src/main.cpp`:
```cpp
bool estop_hw = false;   // gpio_get_level(sys::kEstopGpio) == 0
bool brake_lever = false; // gpio_get_level(sys::kBrakeLeverGpio) == 0
bool mode_btn = false;    // gpio_get_level(sys::kModeBtnGpio) == 0
// ... all GPIO reads are stubbed for test environment
```

### C5. `safety_monitor.cpp` uses test time

```cpp
int64_t g_sys_test_time_us = 0;
int64_t get_time_us() { return g_sys_test_time_us; }
```

Real implementation should use `esp_timer_get_time()`. This is intentionally stubbed for host-based unit testing.

---

## D. Architecture–Implementation Drift

### D1. `0x300` HOST_DRIVE_CMD — missing gear field

| Source | Fields |
|--------|--------|
| `architecture.md` §2.2 | `i32 speed_mmps, i24 yaw_rate_mrad_s, u8 gear` (9 bytes) |
| `shared/can/can_protocol.h` | `i32 speed_mmps, i32 yaw_rate_mrad_s` (8 bytes, NO gear) |

The architecture describes gear as part of the Jetson command. The implementation derives gear from speed direction (step 6, §7.6):

> gear: v > 0 → D, v == 0 → N, v < 0 → R

This is actually **the better design** — gear is a vehicle-level derivation, not a Jetson planning concern. But `main.cpp` line 134 hardcodes gear to 0 (N):

```cpp
can::RtDriveCmd{sp.motor_speed_mmps, uint8_t(0)}.to_frame(fr);  // gear always N
```

**Fix**: Update `architecture.md` §2.2 to match the implementation (remove gear from `0x300`), and implement gear derivation in `t_control`:

```cpp
uint8_t gear = (sp.motor_speed_mmps > 0) ? uint8_t(can::Gear::D)
             : (sp.motor_speed_mmps < 0) ? uint8_t(can::Gear::R)
             : uint8_t(can::Gear::N);
can::RtDriveCmd{sp.motor_speed_mmps, gear}.to_frame(fr);
```

### D2. `0x400` — direction mismatch

| Source | ID name | Direction |
|--------|---------|-----------|
| `architecture.md` §2.2 | `HOST_OBSTACLE_DIST` | Jetson → RT |
| `shared/can/can_protocol.h` | `kIdRtObstacleRpt` | RT → Jetson |

The architecture says Jetson sends obstacle data to RT. The protocol header names it as an RT→Jetson report. These are opposites.

**Analysis**: The architecture is correct — Jetson runs perception (LiDAR/camera), detects obstacles, sends minimum distance to RT over `0x400`. RT's obstacle limiter uses this to clamp speed. The protocol header name `RtObstacleRpt` is misleading.

**Fix**: Rename in `can_protocol.h`:
```cpp
// OLD
constexpr uint32_t kIdRtObstacleRpt = 0x400;
struct RtObstacleRpt { ... };
// NEW
constexpr uint32_t kIdHostObstacleDist = 0x400;
struct HostObstacleDist { ... };
```

### D3. Duplicated CAN ID constants

CAN IDs are defined in **three places**:
1. `shared/can/can_protocol.h` — canonical source
2. `rt-esp32/src/config.h` lines 87-108 — local aliases
3. `sys-esp32/src/config.h` lines 74-89 — local aliases

Both `config.h` files are second sources of truth. If an ID changes in `can_protocol.h` but not in `config.h`, the system silently breaks because `main.cpp` uses `config.h` aliases for dispatch switch statements.

**Fix**: Remove the CAN ID aliases from both `config.h` files. Use `can::kIdRtDriveCmd` etc. directly from the shared header. The aliases add no value — they're the same hex literal.

### D4. `ResolvedSetpoint` — extra field vs architecture

| Source | Fields |
|--------|--------|
| `architecture.md` §7.5 | `motor_speed_mmps, steer_angle_mdeg, gear, steer_valid, reversing` |
| `physics_model.h` | `motor_speed_mmps, steer_angle_mdeg, steer_valid, steer_saturated, reversing` |

Implementation adds `steer_saturated` (missing `gear`) — this is an improvement for telemetry. Update architecture to match.

### D5. SYS heartbeat — class exists but unused

`sys-esp32/src/heartbeat.h` defines `sys::Heartbeat` with `tick()` method. But `main.cpp:367-373` manually builds the heartbeat frame without using the class:

```cpp
// main.cpp task_hb
can::Frame fr;
fr.id  = sys::kIdSysHeartbeat;
fr.dlc = 1;
fr.put_u8(0, ++alive_ctr);
g_can.send(fr);
```

**Fix**: Either use the `sys::Heartbeat` class in `main.cpp`, or delete it. Having both is confusing.

### D6. `Diagnostics` class — report() returns void, caller builds frame

`main.cpp:340-357` calls `g_diag.report(...)` (which only logs), then manually builds and sends `can::SysDiagRpt`. The `Diagnostics` class doesn't own the frame construction.

**Fix**: Have `Diagnostics::report()` return a populated `can::SysDiagRpt` or take a reference and fill it. Then `main.cpp` only sends it.

---

## E. Code Quality Issues

### E1. `physics_model.cpp` — manual `<algorithm>` polyfill

```cpp
#ifndef __cpp_lib_clamp
namespace std {
template<typename T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}
}
#endif
```

This shouldn't be needed with C++17 (`-std=c++17`). If it IS needed, the compiler flags are wrong. This polyfill is also incorrect — it returns `const T&` to a temporary in the ternary.

**Fix**: Remove the polyfill. Ensure `-std=c++17` is in build flags. If the Xtensa toolchain's `<algorithm>` is missing `std::clamp`, add `-std=gnu++17` to `platformio.ini` `build_flags`.

### E2. `ThrottleInput` — type inconsistency

```cpp
int32_t read_mmps() const { return m_speed_mmps; }  // returns int32_t
int16_t tick(uint16_t raw_adc) { ... }               // returns int16_t
int16_t m_speed_mmps = 0;                            // stored as int16_t
```

`m_speed_mmps` is `int16_t` (range ±32767), but `kThrottleMaxSpeedMmps` is 3000 mm/s and operations might overflow above 32767. The widening to `int32_t` on return is correct but the storage should also be `int32_t` for consistency with `can::RtDriveCmd::motor_speed_mmps`.

**Fix**: Change `m_speed_mmps` to `int32_t` in `ThrottleInput`.

### E3. `LightControl` — missing `#include "can/can_protocol.h"`

`light_control.h` uses `can::Mode` in `tick()` signature but only includes `<cstdint>` and `"config.h"`. It relies on transitive includes from `motor_driver.h` or `main.cpp`.

**Fix**: Add `#include "can/can_protocol.h"` to `light_control.h`.

### E4. `Mcp2515Driver::get_error_counters` — calls `read_reg` on const method

```cpp
void Mcp2515Driver::get_error_counters(uint8_t& tec, uint8_t& rec) const {
    tec = read_reg(kRegTec);  // read_reg is non-const (does SPI I/O)
    rec = read_reg(kRegRec);
}
```

This won't compile — `read_reg()` is non-const but called from a const method.

**Fix**: Remove `const` from `get_error_counters`, or make `read_reg` const. Since SPI I/O mutates hardware state, remove `const`.

### E5. `can_rx_router.h` — `GatewayQueues` raw pointers to stack locals

```cpp
// main.cpp t_dispatch
rt::GatewayQueues q;
can::Frame gw_lo, gw_hi;
q.gw_tx_low  = &gw_lo;   // pointer to stack local
q.gw_tx_high = &gw_hi;   // pointer to stack local
```

This works because `route_frame()` is called synchronously before the stack unwinds, but it's fragile. A future refactor that saves `q` for later would use-after-free.

**Fix**: Add a comment noting the synchronous-only constraint, or pass the pointers directly to `route_frame()` instead of through a struct.

---

## F. Missing Files (Phantom IntelliSense References)

Files that appear in old glob/listings but don't exist on disk:

| Phantom file | Status |
|-------------|--------|
| `rt-esp32/src/obstacle_sensor.cpp` | Doesn't exist. IntelliSense errors from cached index. |
| `rt-esp32/src/obstacle_sensor.h` | Doesn't exist. |
| `rt-esp32/src/control_logic.cpp` | Doesn't exist. |
| `rt-esp32/src/control_logic.h` | Doesn't exist. |
| `sys-esp32/src/can_rx_router.cpp` | Doesn't exist. Functionality is in `can_dispatch.h` + `main.cpp`. |
| `sys-esp32/src/can_rx_router.h` | Doesn't exist. Replaced by `can_dispatch.h`. |
| `sys-esp32/src/motor_driver.cpp` | Doesn't exist. `MotorDriver` is header-only in `motor_driver.h`. |
| `sys-esp32/src/speed_limiter.h` | Doesn't exist. Referenced by config comment. |
| `sys-esp32/src/speed_limiter.cpp` | Doesn't exist. |

**Fix**: Clean VS Code IntelliSense cache: `Ctrl+Shift+P` → `C/C++: Reset IntelliSense Database`. Delete any stale `.browse.VC.db` or `ipch/` directories.

---

## G. Fix Priority

| Priority | Issue | Effort | Impact |
|----------|-------|--------|--------|
| **P0** | Add `.vscode/c_cpp_properties.json` or use PlatformIO IDE | 5 min | Fixes ALL IntelliSense errors |
| **P0** | Add `void init() {}` to `IndicatorControl` | 1 min | Unbreaks compile |
| **P1** | Delete `watchdog.cpp` (dead code, wrong class) | 1 min | Removes dead code |
| **P1** | Remove CAN ID duplicates from `config.h` files | 10 min | Single source of truth |
| **P1** | Fix `Mcp2515Driver::get_error_counters` const | 1 min | Fixes compile error |
| **P1** | Remove `std::clamp` polyfill, add `-std=gnu++17` | 5 min | Cleaner code |
| **P2** | Implement gear derivation in `t_control` | 5 min | Matches architecture |
| **P2** | Fix `0x400` direction/naming in shared header | 5 min | Matches architecture |
| **P2** | Fix `diagnostics.cpp` type name or delete file | 5 min | Consistency |
| **P2** | Add `#include "can/can_protocol.h"` to `light_control.h` | 1 min | Self-contained header |
| **P3** | Use `sys::Heartbeat` class in main.cpp or delete it | 5 min | Reduce duplication |
| **P3** | Resolve MotorDriver vs direct DAC/throttle usage in main.cpp | 15 min | Design consistency |
| **P3** | Add `// TODO: I2C` markers to MCP4725 stub | 1 min | Documents gaps |
