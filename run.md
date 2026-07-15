# Running, Testing, and Integration Guide

This document explains how to configure, build, flash, and test the E-Trike Drive-by-Wire control system. The codebase heavily utilizes PlatformIO environments and compile-time feature flags to scale from isolated software unit tests (SIL) to full hardware-in-the-loop (HIL) and production vehicle integration.

---

## 1. PlatformIO Environments (Profiles)

The ESP32 nodes (`rt-esp32` and `sys-esp32`) use PlatformIO environments defined in their respective `platformio.ini` files. 

### `[env:vehicle]` (Default)
**Target:** Production Vehicle (Full Integration)
- Represents the strict, production-ready release configuration.
- **Run Mode:** `ETRIKE_SYSTEM_RUN_MODE=0` (Production Mode).
- **Safety Checks:** All runtime bypasses (`g_bench_solo_mode`, `g_bypass_eps_sync`, etc.) are unconditionally disabled.
- **Use Case:** Flashed onto the actual vehicle for driving operations.

### `[env:hardware_bench]`
**Target:** Hardware Test Bench (HIL)
- Represents a prototype configuration for hardware-in-the-loop testing.
- **Run Mode:** `ETRIKE_SYSTEM_RUN_MODE=1` (Prototype Mode).
- **Safety Checks:** Bypasses are gated behind a physical developer override pin. The system enforces safety checks unless the override pin is actively pulled. 
- **Use Case:** Commissioning hardware actuators, testing real CAN buses without requiring the full vehicle to be assembled.

### `[env:bench]`
**Target:** Software Test Bench (SIL)
- Represents a software-only simulation environment for firmware validation.
- **Run Mode:** `ETRIKE_SYSTEM_RUN_MODE=2` (Simulation Mode).
- **Safety Checks:** Automatically enables software bypasses (`g_bench_solo_mode = true`, etc.) so the firmware does not lock up waiting for missing hardware components (like the MTR or EPS nodes).
- **Features:** Usually configured with `Calculated` speed feedback and `Shadow` PID mode (see RT feature flags below) so the system simulates physical momentum without physical encoders.
- **Use Case:** Debugging logical faults, CAN validation, and telemetry viewing without physical motors or actuators.

### `[env:native]`
**Target:** Host PC (Unit Testing)
- Runs tests locally on the host machine using `TDM-GCC` or similar (Windows/Linux/macOS).
- Mocks hardware abstractions (`native-test/hal/shadow`).
- **Use Case:** Extremely fast regression testing and continuous integration.

---

## 2. Real-Time (RT) Compile-Time Feature Flags

The `rt-esp32` node uses strict compile-time type aliasing to eliminate branch overhead in the 100Hz real-time loop. These flags are defined in `platformio.ini` and validated by `build_config.h`.

| Flag | Value | Description |
|------|-------|-------------|
| `ETRIKE_RT_KINEMATICS_RESOLVER` | `0` | **Bicycle Physics:** Uses `PhysicsModel` with three speed regimes (Standard). |
| | `1` | **Direct Passthrough:** Uses `DirectResolver` (stateless direct mapping). |
| `ETRIKE_RT_SPEED_FEEDBACK_SOURCE` | `0` | **None:** Open-loop (no feedback). |
| | `1` | **MTR Report:** Telemetry only. |
| | `2` | **RT Encoder:** Uses physical PCNT quadrature encoders. |
| | `3` | **Calculated:** Uses a first-order lag plant model (perfect for `bench` mode SIL). |
| `ETRIKE_RT_PID_MODE` | `0` | **Disabled:** No PID calculations. |
| | `1` | **Shadow:** Calculates PID for telemetry tracking only (no motor output effect). |
| | `2` | **Active:** Injects PID corrections directly into the drive setpoint. |
| `ETRIKE_RT_ENCODERS` | `0` | **Disabled:** PCNT hardware off (prevents noise on floating pins). |
| | `1` | **Enabled:** PCNT hardware on (requires physical encoders wired up). |

---

## 3. Runtime System Run Modes

The global system run mode is controlled by `ETRIKE_SYSTEM_RUN_MODE`. This determines the strictness of the firmware initialization.

1. **Production Mode (`0`)**: 
   - Strict adherence. `g_bench_solo_mode = false`, `g_bypass_eps_sync = false`, `g_bypass_seb_sync = false`, `g_bypass_mtr_absent = false`.
   - If an actuator goes offline, the system throws a HARA fault and ESTOPs.
2. **Prototype Mode (`1`)**:
   - Reads a physical hardware override pin. If active, bypasses are allowed. If inactive, acts exactly like Production mode.
3. **Simulation Mode (`2`)**:
   - Explicitly bypasses missing physical components for software testing. `TESTING` flag is usually defined here.

---

## 4. How to Build, Flash, and Test

### Building Firmware
Navigate into the workspace (e.g., `rt-esp32` or `sys-esp32`) and run PlatformIO commands:
```bash
# Build the default (vehicle) environment
pio run

# Build a specific environment (e.g., bench)
pio run -e bench
```

### Flashing to Hardware
Connect your ESP32-S3 via USB and run:
```bash
# Flash the vehicle production profile
pio run -t upload -e vehicle

# Flash the HIL bench profile
pio run -t upload -e hardware_bench

# Flash and open serial monitor
pio run -t upload -t monitor -e bench
```

### Running Native Unit Tests (SIL)
Native tests run on your local PC. They do not require hardware and execute in milliseconds. They are crucial for testing control logic, state machines, and CAN encodings.
```bash
# From rt-esp32 or sys-esp32 directory
pio test -e native
```
> **Tip:** You can increase verbosity of test outputs with `pio test -e native -v`.

---

## 5. Software Debugging and Tooling

The repository includes a web-based `debug-tool` (`@etrike/debug-backend`, `@etrike/debug-simulator`, `@etrike/debug-ui`) that connects to the system. 
- Use the **Simulator** package to generate virtual CAN traffic to test the ESP32 boards in `bench` mode.
- Use the **UI** to visualize telemetry, view system state, and send debug commands.

**To run the debug tool:**
1. Navigate to `debug-tool`.
2. Run `npm install`.
3. Start the relevant workspaces (e.g., `npm run dev --workspace=@etrike/debug-ui`).

---

## 6. Integration Checklist (From Desk to Vehicle)

1. **Phase 1: Software Unit Testing (Native)**
   - Run `pio test -e native`. Ensure 100% pass rate.
   - Modifies purely software logic.
2. **Phase 2: SIL Simulation Bench (`bench`)**
   - Flash `bench` environment. Use the `debug-tool` simulator to pump virtual CAN traffic.
   - Verify state transitions, ESTOP logic, and RT feature flags (like `Calculated` speed estimation).
3. **Phase 3: HIL Hardware Bench (`hardware_bench`)**
   - Flash `hardware_bench` environment. Connect actual actuators (EPS/SEB) but not necessarily the full drivetrain.
   - Verify electrical integration, CAN transceiver reliability, and actual actuator movement.
4. **Phase 4: Full Vehicle Production (`vehicle`)**
   - Flash `vehicle` environment.
   - Execute the strict testing protocols defined in `commissioning-test-profiles.md`.
