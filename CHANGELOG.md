# Changelog

All notable changes to the E-Trike Drive-by-Wire Control System.

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
