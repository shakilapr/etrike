# Dead Code Audit — E-Trike Codebase

Audit date: 2026-06-14. Every active source file checked against current architecture.

---

## ACTIVE directory dead code (referenced but never used, or missing)

### D1. `dispatch_frame()` — never called
**File:** `sys-esp32/src/can_dispatch.h`
**Problem:** `dispatch_frame()` is defined but never called. The SYS dispatch task in `main.cpp` uses a manual `switch(fr.id)` statement instead. Only `DispatchTargets` type is referenced (for a variable that's created with all-null pointers and then discarded).
**Impact:** ~30 lines of dead code. Includes misleading comment about routing policy.
**Fix:** Either wire `dispatch_frame()` into the dispatch task, or delete `can_dispatch.h`.

### D2. `sys::Heartbeat` class — never instantiated
**File:** `sys-esp32/src/heartbeat.h`
**Problem:** Defines a `Heartbeat` class with `init()`, `tick()`, `counter()`. Never instantiated in `main.cpp`. The heartbeat task builds CAN frames inline (`fr.id = sys::kIdSysHeartbeat; fr.dlc = 1; fr.put_u8(0, ++alive_ctr);`).
**Impact:** 28 lines of dead class. Duplicate logic with inline code in main.
**Fix:** Wire the class into the heartbeat task, or delete `heartbeat.h`.

### D3. `sys::Diagnostics::report()` — output discarded
**File:** `sys-esp32/src/diagnostics.h`, `diagnostics.cpp`
**Problem:** `g_diag.report()` is called in `task_diag`, but its output is never used. Immediately after the call, the task manually reassembles the same data into a `SysDiagRpt` struct and sends the CAN frame. The `report()` call is effectively a no-op — it logs via ESP_LOGD but doesn't produce the CAN frame.
**Impact:** `diagnostics.cpp` (~30 lines) is wasted work; the `report()` call in main is misleading.
**Fix:** Have `report()` build and return the CAN frame, or remove `diagnostics.h/.cpp`.

### D4. `sys::MotorDriver` — never ticked
**File:** `sys-esp32/src/motor_driver.h`
**Problem:** `g_motor` is constructed and `init()` is called in `app_main`, but `g_motor.tick()` is never called. The motor task uses `g_dac` and `g_throttle` directly. `g_motor` owns its own `Mcp4725Dac` and `ThrottleInput` instances that are entirely unused — the task uses the global `g_dac` and `g_throttle` instead.
**Impact:** `motor_driver.h` (~25 lines) is fully dead. `g_motor.init()` in app_main initializes duplicate DAC/throttle objects that are never used.
**Fix:** Either wire `g_motor.tick()` into the motor task and remove the inline logic, or delete `motor_driver.h` and `g_motor`.

### D5. `sys::speed_limiter.h` / `speed_limiter.cpp` — never included
**Files:** `sys-esp32/src/speed_limiter.h`, `sys-esp32/src/speed_limiter.cpp`
**Problem:** Define `limit_forward_speed_for_obstacle()` — never included or called from `main.cpp`. The `config.h` comment says "for speed_limiter.cpp" but this is stale. Obstacle speed limiting is done on RT (via `PhysicsModel::obstacle_limit()`), not SYS.
**Impact:** ~20 lines of dead code in active src. Test `test_speed_limiter.cpp` tests dead code.
**Fix:** Delete `speed_limiter.h/.cpp` from active src. Already exists in legacy.

### D6. RT: missing obstacle task
**File:** `rt-esp32/src/main.cpp`
**Problem:** `g_obstacle_mm` atomic is declared but never written to. `obstacle_sensor.h` is not included. No task polls the HC-SR04. `g_obstacle_mm.load()` in control always returns `UINT32_MAX`, making `obstacle_limit()` always return full speed.
**Architecture says:** obstacle task at prio 2, 10 Hz (§7.7).
**Impact:** Obstacle detection non-functional. All obstacle speed limiting is bypassed.
**Fix:** Add obstacle task back to main.cpp (poll HC-SR04, update g_obstacle_mm atomic).

### D7. `is_forwarded_low_to_high()` / `is_forwarded_high_to_low()` — only tests
**File:** `shared/can/can_protocol.h`
**Problem:** Defined but never called by any production code. Only referenced from `test_can_protocol.cpp`. Both RT and SYS dispatch tasks use manual switch/case.
**Impact:** ~10 lines of dead utility functions in the shared protocol header.
**Fix:** Remove or keep as documentation helpers (harmless in header-only form).

### D8. `can::CanDriver::send_blocking()` — never called
**File:** `shared/can/can_driver.h`
**Problem:** `send_blocking()` method defined (~10 lines). Never called anywhere in the codebase.
**Impact:** Dead method. Could be useful for ESTOP frames (which should be blocking per architecture principle #2), but not currently used.
**Fix:** Wire into ESTOP paths, or remove.

---

## LEGACY directory dead code (acknowledged as superseded)

These files in `legacy/` are correctly isolated. They are listed for completeness:

| File | Superseded by | Status |
|------|--------------|--------|
| `legacy/rt-esp32/src/speed_pid.h/.cpp` | PID deferred (gap #5); gains in config.h | OK in legacy |
| `legacy/rt-esp32/src/control_logic.h/.cpp` | Inline control in RT main.cpp | OK in legacy |
| `legacy/rt-esp32/src/steering_servo.h/.cpp` | `steering_control.h` (SYNTREE CAN) | OK in legacy |
| `legacy/rt-esp32/src/main.cpp` | Current `rt-esp32/src/main.cpp` | OK in legacy |
| `legacy/shared/intermcu/*` | CAN-based communication | OK in legacy |
| `legacy/sys-esp32/src/brake_actuator.h/.cpp` | `brake_control.h` (SEB CAN) | OK in legacy |
| `legacy/sys-esp32/src/can_rx_router.h/.cpp` | Inline dispatch in SYS main.cpp | OK in legacy |
| `legacy/sys-esp32/src/motor_driver.cpp` | MCP4725 DAC (header in active) | OK in legacy |
| `legacy/sys-esp32/src/speed_limiter.h/.cpp` | Also in active; dead there too | Duplicate in both |
| `legacy/sys-esp32/src/throttle_input.cpp` | `throttle_input.h` (active) | OK in legacy |

---

## CONFIG: deleted constant causing test failure

### C1. `sys::kMotorMaxSpeedMmps` removed from config.h
**File:** `sys-esp32/src/config.h` (linter-modified), `sys-esp32/test/test_sys_config.cpp`
**Problem:** The linter removed the deprecated constants block that included `kMotorMaxSpeedMmps`, `kBrakeGpio`, `kMotorPwmGpio`, `kMotorPwmFreqHz`, `kPwmMax`. But `test_sys_config.cpp` still checks `sys::kMotorMaxSpeedMmps == 3000`. This test will fail to compile.
**Impact:** Link error when building SYS config test.
**Fix:** Remove the `kMotorMaxSpeedMmps` check from the test, or re-add the constant (it's used in `mcp4725_dac.h` via `kThrottleMaxSpeedMmps` which is still there).

---

## Summary

| Category | Count | Severity |
|----------|-------|----------|
| Dead classes/functions in active src | 5 | 🟡 Medium |
| Dead utility functions | 2 | 🔵 Minor |
| Missing task (obstacle) | 1 | 🟠 Major |
| Config breakage | 1 | 🟡 Medium |
| Legacy (correctly isolated) | 10 | — status OK |

### Recommended fixes (priority order)

1. **Add obstacle task back to RT main.cpp** — restores obstacle detection per arch §7.7
2. **Delete `can_dispatch.h`** — unused; redundant with inline dispatch
3. **Delete `sys::Heartbeat`** or wire it into task_hb
4. **Delete `sys::Diagnostics::report()`** or have it actually send the CAN frame
5. **Delete `sys::MotorDriver`** — `g_motor` is never ticked
6. **Delete `sys::speed_limiter.h/.cpp`** — obstacle limiting is on RT, not SYS
7. **Remove `kMotorMaxSpeedMmps` from test** or re-add to config
8. **Remove `send_blocking()`** or wire it into ESTOP paths
9. **Remove `is_forwarded_*()` helpers** — harmless but misleading
