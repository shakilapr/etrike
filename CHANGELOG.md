# Changelog

All notable changes to the E-Trike Drive-by-Wire Control System.

## [1.0.0-alpha] — 2026-07-02

### Summary

This release marks the completion of the core safety architecture and the resolution of 28 FATAL bugs identified during QA audits (bugs 4.1-9.4, D2). All critical safety mechanisms -- ESTOP propagation, heartbeat monitoring, steering following error, command staleness, checksum validation, DLC guards, and rolling counters -- are implemented and verified at the unit and integration test level. Vehicle production build profiles with zero safety bypasses are available.

### FATAL Bugs Fixed

**Six FATAL brake and ESTOP bugs in RT firmware (bugs 4.1-4.10):**
- Bug 4.1: Brake zeroing -- dual sender collision on 0x7B9 resolved by RT suppressing 0x7B9 in ESTOP/FAULT states
- Bug 4.2: Brake zeroing -- RT 0x7B9 overriding SYS brake on ESTOP
- Bug 4.10: SEB alignment bit uninitialized (align_enable=0 when braking requested)

**Seven FATAL SYS and MTR bugs (bugs 5.3-6.3):**
- Bug 5.3: ESTOP rate-limit missing in SYS dispatch
- Bug 6.1: ESTOP queue bypass timeout in CAN dispatch
- Bug 6.2: ECU temperature underflow in diagnostics
- Bug 6.3: ESTOP rate-limiting not applied to TX path
- Gap #14: Rate-limit constants configured (kEstopRateLimitWindowMs=1000, kEstopRateLimitMax=2)

**Five FATAL physics, SPI, bus-off, and checksum bugs in RT firmware (bugs 7.1-7.5):**
- Bug 7.1: Physics model following error threshold off-by-one
- Bug 7.2: SPI bus-off recovery missing retry
- Bug 7.3: Steering checksum validation not rejecting corrupted frames
- Bug 7.4: Brake 0x7B9 checksum not validated on RX
- Bug 7.5: EPS-C 0x201 rolling counter not checked for frozen counter

**Seven FATAL SYS and MTR bugs in QA pass 3 (bugs 8.1-8.5):**
- Bug 8.1: MCP4725 I2C infinite timeout (HAL_MAX_DELAY blocks ESTOP)
- Bug 8.2: I2C deadlock blocks DAC write, defeating hardware ESTOP -- fixed with finite 100ms timeout
- Bug 8.3: MTR task watchdog not monitoring all tasks
- Bug 8.4: MTR gear relay stuck-on on cold boot
- Bug 8.5: MTR 0x204 staleness not triggering in all modes

**Four FATAL debug tool backend bugs (bugs 9.1-9.4):**
- Bug 9.1: Zombie WebSocket connections accumulate memory -- fixed with ping/pong eviction at 60s timeout
- Bug 9.2: WebSocket close handler leaks client references
- Bug 9.3: API allows ESTOP injection without confirmation
- Bug 9.4: Stream broadcast crashes on undefined readyState

**CAN protocol bugs (D2):**
- Bug D2: CAN DLC generation ignoring explicit dlc: fields in YAML -- all frames now respect protocol-specified DLC

### New Features

**Vehicle Production Build Profiles:**
- `CONFIG_BENCH_SOLO`, `BYPASS_EPS_C_SYNC`, `BYPASS_SEB_SYNC`, `BYPASS_MTR_ABSENT` flags for bench testing
- Vehicle build profile with zero safety bypasses for production
- Bench bypass verification: 10 tests across 5 bypass scenarios
- Per-vehicle build configuration in CI

**ESTOP Reason Codes:**
- Complete ESTOP reason codes tracked in 0x210 safety_state telemetry
- Reasons: kEstopReasonButton, kEstopReasonHeartbeat, kEstopReasonFollowingError, kEstopReasonStaleCmd, kEstopReasonCanBusOff, kEstopReasonEgasMismatch, kEstopReasonWatchdog
- ESTOP reason codes visible in debug tool dashboard

**Steering State Telemetry:**
- Steering state (BOOT_WAIT, LISTEN_SYNC, ACTIVE, FAULT) added to 0x210 RT_SAFETY_STATE
- Real-time steering health visible via CAN telemetry

**Heartbeat Health Flags:**
- Standard health flag bit layout in heartbeat frames (DLC=2)
- Flags: heartbeat_ok, task_watchdog_ok, mode_ok, estop_ok
- Health flags visible across all nodes

**Task Health Monitoring:**
- Per-task alive counters with stall detection
- CAN RX overflow tracking in diagnostic telemetry
- Persistent failure counters (replacing warned-once patterns)
- Framework safety hardening: FreeRTOS hooks, stack canary, NVRAM crash persistence

**Communication Stack Hardening:**
- CAN bus-off detection and auto-recovery (RT + SYS nodes)
- MCP2515 TXB2 reserved for ESTOP (highest-priority buffer)
- TX error recovery with retry
- Gateway forwarding priority: ESTOP (0x001) gets send-to-front in both directions

### Safety Mechanisms

- **Heartbeat monitoring (triple-node):** RT monitors Host (0x7FC) and SYS (0x7FE); SYS monitors RT (0x7FD); Host monitors RT (0x7FD). Independent timeout per link.
- **Command staleness guard:** RT monitors 0x300 at 500ms; SYS and MTR monitor 0x204 at 200ms; 3-second startup grace period.
- **Steering following error:** Speed-scaled threshold (2-10 deg depending on speed) with 300ms persistence before ESTOP.
- **ESTOP propagation:** CAN 0x001 on both buses, priority forwarding, rate-limited (max 2 per 1000ms window).
- **XOR checksum validation:** All steer-by-wire frames (0x169, 0x201, 0x721, 0x7B9) use XOR(bytes[0..6]) ^ 0xFF.
- **Rolling counter guards:** 4-bit rolling counter on all actuator frames; frozen counter detection.
- **EGAS L2 speed monitoring:** SYS compares 0x204 setpoint vs 0x206 actual speed at 500mm/s threshold with 500ms persistence.
- **DLC validation guards:** All from_frame() methods reject wrong DLC sizes; steer-by-wire frames require DLC >= 8.
- **External watchdog (TPS3850):** Independent RC oscillator, 100ms timeout, GPIO toggle on both RT and SYS.
- **Internal task watchdog:** Per-task alive counters with stall detection across all FreeRTOS tasks.

### Known Limitations

1. **MTR STM32 drivers need hardware testing.** The MCP4725 DAC I2C driver and STM32 CAN driver have been reviewed and tested in simulation but not on physical hardware. The finite I2C timeout fix (bug 8.2) is verified at the unit test level only.
2. **HIL safety tests pending.** All 19 HIL test scenarios (Tiers 1-4) are defined in `docs/hil-safety-test-plan.md` but none have been executed on physical hardware. Unit tests and integration tests provide pre-HIL verification only.
3. **No formal certification.** The system has not undergone ISO 26262 or any other functional safety certification. ASIL designations in architecture docs are targets, not certified claims.
4. **SEB comm-fault behavior unverified.** When SYS resets (watchdog), SEB enters internal comm-fault after 20ms without 0x7B9. Whether the SEB holds or releases brake pressure during this window is empirically unverified. A hardware brake-hold relay is recommended.
5. **Cornering rollover physics model not validated.** The lateral acceleration rollover threshold in the physics model has not been validated against real vehicle dynamics.
6. **Autoware.Auto bridge is alpha-quality.** The `autoware_vehicle_bridge` node has been tested in simulation only.

### Build Instructions

For vehicle production build (zero bypasses):
```
cd rt-esp32
idf.py set-target esp32s3
idf.py build
```

For bench testing with bypasses:
```
cd rt-esp32
idf.py menuconfig  # Enable CONFIG_BENCH_SOLO and desired BYPASS flags
idf.py build
```

SYS and MTR builds follow the same pattern. See individual `config.h` files for per-module configuration.

### Hardware Compatibility

| Component | Specification | Status |
|-----------|--------------|--------|
| RT Controller | ESP32-S3 @ 240 MHz, 8MB PSRAM, 16MB flash | Verified |
| SYS Controller | ESP32-S3 @ 240 MHz, 8MB PSRAM, 16MB flash | Verified |
| MTR Controller | STM32 (TBD variant) | Driver level only |
| Host Computer | NVIDIA Jetson Orin NX, 8GB | Verified |
| Powertrain Gateway | ESP32-S3 | Config defined |
| High CAN Bus | MCP2515 + SN65HVD230 @ 1 Mbps | Verified |
| Low CAN Bus | TWAI + SN65HVD230 @ 500 kbps | Verified |
| Steering Actuator | EPS-C (SYNTREE) | Protocol verified |
| Brake Actuator | SEB (SYNTREE) | Protocol verified |
| Motor Controller | Kelly (or compatible) via 0-5V throttle | Bench tested |
| ESTOP Button | NC mushroom, dual-path to SYS and MTR | Designed |
| Watchdog IC | TPS3850 (programmable) | Designed |
| DC-DC Converter | 72V-to-12V, CAN 0x012 controlled | Protocol defined |
| Throttle DAC | MCP4725, I2C, 0-5V output | Driver level only |

### CI Pipeline

- Static analysis CI job
- Backend test CI job (vitest)
- Vehicle build CI job (production profile with zero bypasses)
- Bypass audit CI job
- Frontend tests (117+ tests)
- Backend tests (128+ tests)
- Native test suite (safety features, components, protocol, checksum, DLC)

### Contributors

30+ commits across 6 repository modules: `rt-esp32`, `sys-esp32`, `mtr-stm32`, `debug-tool`, `shared`, `native-test`, `simulation`, `jetson`.

---

## [0.1.0-alpha] — 2026-07-02

### CAN Infrastructure
- **MCP2515 CAN controller driver** — full SPI driver with mode switching and bus-off detection
- **CAN TX error handling** — corrected CAN channel bus assignments, enhanced TX error recovery
- **DLC consistency check** — validation layer ensuring DLC matches protocol expectations
- **Gateway forwarding rules tests** — verify correct message routing between CAN buses

### Safety
- **READY & ESTOP bulb indicators** — GPIO-driven physical indicators for system state visibility
- CAN bus-off detection and auto-recovery hardened across all nodes

### Documentation
- Wiring documentation updated for consistency and clarity
- Comprehensive ISO 26262 functional safety report (`tem/safety-doc.md`)

### Code Quality
- Refactored code structure across multiple modules for readability and maintainability

---

## [0.0.6-alpha] — 2026-06-28

### Design Gap Closure
- **All Known Design Gaps resolved** — gap section removed from architecture docs
- Bench bypass flags: `CONFIG_BENCH_SOLO`, `BYPASS_EPS_C_SYNC`, `BYPASS_SEB_SYNC`, `BYPASS_MTR_ABSENT`
- Bench bypass verification: 10 tests across 5 bypass scenarios
- S2 fix: 0x210 on both buses, SYS reads RT safety_state for takeover detection
- Gap 2+3: RT safety_state on 0x210, gate actuator TX on MANUAL mode

### Testing
- **Integration tests T1–T6**: SYS dispatch native test, multi-task watchdog, CAN saturation, ignition sequence, MTR handshake, mode transition
- Integration test procedure + FMEA light documentation
- SYS per-task alive counters with fault injection tests

### Debug Tool
- **CAN table generators**: interactive HTML viewer, A3 LaTeX PDF, CSV fixes
- BitGrid, SignalTable, CanDictionary, MessageCard components for CAN visualization
- Health strip header, transport badge, error log, CAN index generator
- CAN frame overflow tracking and improved logging
- Setup and installation scripts

### Protocol
- Heartbeat DLC 1→2 with health byte for field debugging
- MTR startup ready flag, PWT heartbeat YAML
- Autoware bridge: standard global topic names, GAP-1 resolved

### Architecture
- Architecture sections §7, §8, §12 fully updated
- Debug tool bench templates + architecture §9 bench bypass section
- Dual-path ignition (GPIO + CAN) added to SYS configuration

### Fixes
- RT TX jitter: `vTaskDelay(5)` → `vTaskDelayUntil` for precise 5ms period
- CAN table columns optimized to 9 cols covering all YAML data
- YAML syntax error from heartbeat DLC update
- Bridge audit: tier4 dep, DLC drift, dead code, steering clamp, thread safety
- Remove Diagnostics dead code, fix brake test UB

---

## [0.0.5-alpha] — 2026-06-27

### Debug Tool 2.0
- **Dashboard redesign**: responsive engineering dashboard with UnitTest component
- **Pipeline tab**: visualize high→low bus causal chain
- **Keyboard controls**: HUD overlay for continuous keyboard controller
- CAN Monitor redesigned as grouped category cards
- Auto-detect CAN bus from frame IDs
- Transport-agnostic adapter labels in UI

### CAN Infrastructure
- **CAN bus-off detection and auto-recovery** — RT + SYS nodes
- CAN catalog synced to 37 IDs, backend type/catalog tests (128 tests)
- YAML-driven DBC generator replaces 3 procedural generators
- CANalyst-II process bridge (Python + TypeScript)
- HardwareBridge interface with serial and CANalyst-II implementations

### Simulation Harness
- ECU models for all six vehicle nodes
- Physics models and RT control algorithms
- Simulation CLI, validation scenarios, and integration tests

### Safety Fixes
- RT steering ESTOP safety gaps closed
- SYS config constant corrections
- Independent heartbeat counters per CAN bus
- External watchdog GPIO toggle in RT control_task
- Brake arbitration gap closure with 0x203 RT_BRAKE_CMD

### CI / Testing
- Static analysis CI job
- Frontend tests expanded from 21 to 117
- Vitest infrastructure for backend tests
- Timing budget documentation

### Protocol
- STEER_DIAG (0x310) and BRAKE_DIAG (0x311) telemetry frames added
- Speed bits 8-9 added to VcuSesReq byte 5
- SYNTREE checksum algorithm corrected to XOR
- Autoware.Auto Vehicle Interface I/O documentation

### Build
- ESP-IDF 6.x TWAI API migration
- `-Wno-error` + build_unflags for ESP-IDF 6.x compatibility
- Rename Jetson → Host across codebase

---

## [0.0.4-alpha] — 2026-06-24

### Autoware.Auto Integration
- ROS 2 `autoware_vehicle_bridge` lifecycle node
- Vehicle bridge with kinematic state publishing
- Autoware.Auto Vehicle Interface documentation

### Debug Tool Initial Launch
- Backend (Fastify + WebSocket), UI (Svelte + Vite)
- Dual-bus CAN monitor with transport abstraction
- MQTT → serial transport migration
- Dark theme, reconnection logic, configuration via zod
- Architecture documentation for dual-bus diagnostic tool

### CAN Database
- DBC outputs consolidated to `shared/can/`
- Per-component DBCs via canmatrix
- YAML signal dictionary with sender, naming, scaling corrections

### Simulation
- Project skeleton with core modules and bus model
- Physics models and RT control algorithms
- ECU models for all vehicle nodes
- Validation scenarios and CLI interface

### Protocol
- STEER_DIAG (0x310) and BRAKE_DIAG (0x311) telemetry frames
- EPS-C checksum corrected from SUM to XOR
- 0x6FA SES_Test byte layout fix
- CAN signal definitions aligned with manufacturer DBC specification

### Code Quality
- Codebase audit for dead code and stale references
- Coding conventions normalized across modules
- Magic numbers replaced with named constants

---

## [0.0.3-alpha] — 2026-06-23

### Motor Controller
- **MTR STM32 motor controller firmware** (4th ECU node)
- EGAS 3-level motor safety architecture
- Option D: mode-gated dual control of SYNTREE actuators

### CAN Protocol
- CAN IDs corrected to match SYNTREE CSV factory defaults (row-by-row audit)
- CAN wire protocol encoding errors fixed
- 18 CAN tests passed after protocol field fixes
- Dual-bus CAN topology: low-speed (500 kbps) and high-speed (1 Mbps)

### Architecture
- **AURIX Lite consolidated architecture** variant (`rt-aurix-lite`)
- RT + SYS combined into single AURIX TC3xx on one CAN bus
- Architecture and CAN dictionary unified
- Responsibility split documented (Jetson/RT/SYS/MTR/PWT)

### Safety
- ESTOP behavior standardized across nodes
- Missing safety checks added to control paths
- Mode-gated actuator control (MANUAL/AUTO/ESTOP)

### Documentation
- Wiring harness documentation with GPIO tables
- I/O data documentation for all system components
- STM32 motor controller config + 0x206 protocol struct

---

## [0.0.2-alpha] — 2026-06-15

### RT Firmware (Real-Time Controller)
- CAN drivers, heartbeat monitor, gateway router (10 tests)
- Tricycle kinematics, steering state machine (11 tests)
- Obstacle speed limit, brake arbitration
- Dynamic angle clamp + speed-scaled following error
- Watchdog + full pipeline test (7 tests)

### SYS Firmware (System Body Controller)
- CAN driver wrapper, config header
- Heartbeat monitor (0x7FE, alive counter, wrap at 255)
- Mode manager (button debounce + transitions)
- Safety monitor + mode integration (13 tests)
- Throttle ADC, MCP4725 I2C DAC
- Motor driver, gear control, CAN dispatch
- Brake SEB control (boot SM, stroke/pressure, roll counter)
- DCDC, lights, indicators, WDT
- Main skeleton (15 tasks, 2 queues)

### Build System
- Both RT and SYS projects build successfully on ESP-IDF 6.12.0
- ESP-IDF 6.x TWAI API migration
- ESP_PLATFORM guard for can_driver.h with host stubs
- `*.exe` build artifacts added to .gitignore

### Protocol
- CAN protocol header rewritten for 3-node architecture
- Inter-MCU link moved to legacy
- CAN IDs aligned with SYNTREE CSV factory defaults

---

## [0.0.1-alpha] — 2026-06-12

### Initial Architecture
- First commit: project skeleton and CAN concept
- Dual-ESP32 → single ESP32-S3 unified architecture
- CAN bus split: low-speed (body/actuators) and high-speed (perception)
- CAN dictionary with per-message signal definitions
- Responsibility split across ECUs

### Documentation
- Unified architecture document
- Cross-references to theory notes and reference docs
- Wiring and I/O documentation scaffolding
- CAN steering, throttle/gear, body control topology

### Protocol Design
- CAN message ID assignments for all nodes
- Heartbeat protocol with liveness matrix
- 3-category CAN forwarding (gateway, passthrough, terminate)
- Cross-document conflict resolution (10 issues closed)
- Legacy files moved to `legacy/` folder
