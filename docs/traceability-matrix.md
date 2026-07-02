# Requirements Traceability Matrix

> **Project:** E-Trike Drive-by-Wire Control System
> **Version:** 1.0.0-alpha
> **Date:** 2026-07-02

Traceability from HARA hazards through safety goals, functional and technical requirements to implementation, test, and verification evidence.

---

## Hazard Analysis (HARA)

| Hazard ID | Hazard Description | Risk Level | Source |
|-----------|-------------------|------------|--------|
| HAZ-01 | Unintended acceleration (motor runaway) | ASIL-C | EGAS analysis, FMEA |
| HAZ-02 | Loss of braking during operation | ASIL-D | FMEA brake-steer-motor |
| HAZ-03 | Unintended steering command (loss of directional control) | ASIL-C | FMEA brake-steer-motor |
| HAZ-04 | Loss of steering control (actuator comms failure) | ASIL-C | FMEA brake-steer-motor |
| HAZ-05 | Rollover from excessive steering angle at speed | ASIL-B | Physics model, architecture |
| HAZ-06 | Loss of CAN communication between nodes | ASIL-C | Distributed architecture |
| HAZ-07 | MCU firmware hang or crash | ASIL-D | External watchdog doc |
| HAZ-08 | ESTOP fails to engage when triggered | ASIL-D | Defense-in-depth doc |
| HAZ-09 | Mechanical steering linkage jam | ASIL-C | Defense-in-depth doc |
| HAZ-10 | Brake actuator (SEB) communication loss | ASIL-D | Emergency system doc |
| HAZ-11 | CAN frame corruption (bit flip, CRC error) | ASIL-C | Steering security protocol |
| HAZ-12 | Debug tool zombie connections accumulate memory | QM | Bug 9.x analysis |
| HAZ-13 | Dual CAN sender collision on 0x7B9 brake command | ASIL-D | Bug 4.x analysis |

---

## Traceability Matrix

### HAZ-01: Unintended Acceleration (Motor Runaway)

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-01: Motor must stop within 200ms of ESTOP | FR-01: MCP4725 DAC output must go to 0V on ESTOP | TR-01a: DAC write 0V in ESTOP handler | `mtr-stm32/src/mcp4725_dac.h:37-43` — I2C write with finite timeout | `test_safety_features.cpp` — EGAS L2 mismatch test (S6, line 154) | HIL T2.1: DAC output probe | Needs HIL |
| SG-01 | FR-02: Gear relays must open (Neutral) on ESTOP | TR-02: Gear GPIOs set to tri-state on ESTOP | `sys-esp32/src/main.cpp:869` — ESTOP handler sets gear OFF | `test_safety_features.cpp` — ESTOP response test | HIL T2.2: Relay coil probe | Needs HIL |
| SG-01 | FR-03: MTR must detect 0x204 staleness within 200ms | TR-03: `kCmdStaleTimeoutMs = 200` in MTR config | `mtr-stm32/src/main.cpp:189-195` — staleness check | `test_components.cpp:301-311` — command staleness test | Integration test T5 | Verified |
| SG-01 | FR-04: SYS must monitor EGAS L2 speed mismatch | TR-04: Compare 0x204 setpoint vs 0x206 feedback | `sys-esp32/src/main.cpp:428-457` — EGAS L2 comparison | `test_safety_features.cpp:154-170` (S6) | Integration test | Verified |

### HAZ-02: Loss of Braking

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-02: Brake must engage fully on ESTOP | FR-05: CAN 0x7B9 must transmit stroke=max on ESTOP | TR-05a: SYS ESTOP handler sets SEB stroke to max | `sys-esp32/src/main.cpp:446` — SEB stroke=max on ESTOP | `test_safety_features.cpp:81` — ESTOP response test | Integration test T2 | Verified |
| SG-02 | FR-06: RT must take over 0x7B9 if SYS heartbeat lost | TR-06: RT detects SYS heartbeat timeout and sends 0x7B9 | `rt-esp32/src/safety_monitor.h:91-109` — SYS timeout triggers brake takeover | `test_safety_features.cpp:48-76` (S2) | Integration test T3 | Verified |
| SG-02 | FR-07: Brake lever (GPIO2) must actuate SEB in MANUAL mode | TR-07: SYS reads GPIO2 and transmits 0x7B9 with lever stroke | `sys-esp32/src/brake_control.h:72` — lever-based stroke in MANUAL | `test_sys_can_dispatch.cpp:40-47` — DLC guard test | HIL T3.1: Brake lever physical test | Needs HIL |
| SG-02 | FR-08: Avoid dual-sender collision on 0x7B9 | TR-08: RT suppresses 0x7B9 in ESTOP/FAULT states | `rt-esp32/src/main.cpp:439-448` — RT suppresses 0x7B9 to avoid collision | Bug 4.1 test: `test_qa_bugs.cpp` | Code review, unit test | Verified |
| SG-02 | FR-09: SEB alignment bit must be set on brake commands | TR-09: `align_enable` must be 1 when braking requested | `sys-esp32/src/brake_control.h:72` — alignment handling | Bug 4.10 test: `test_qa_bugs.cpp:52-77` | Unit test | Verified |

### HAZ-03: Unintended Steering Command

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-03: Steering must not exceed safe angle envelope | FR-10: RT must clamp all commanded steering angles to +/-40 deg | TR-10: Software hard-stop before any command transmission | `rt-esp32/src/main.cpp` — hard-stop clamp on commanded angle | `test_safety_features.cpp` — dynamic clamp test | Code audit | Verified |
| SG-03 | FR-11: RT must apply speed-dependent dynamic angle clamp | TR-11: `physics_resolve()` clamps angle inversely to speed | `rt-esp32/src/physics_model.h:49-50` — dynamic clamp calculation | `test_components.cpp:90-93` — following error threshold test | Code audit | Verified |
| SG-03 | FR-12: ESTOP must stop transmitting 0x169 (silent-stop) | TR-12: ESTOP handler stops 0x169 TX | `rt-esp32/src/main.cpp:439-448` — command suppression | `test_safety_features.cpp` — ESTOP behavior test | Integration test | Verified |
| SG-03 | FR-13: Steering must obey Listen Before Speaking on boot | TR-13: Wait for 0x201 status, sync to current angle before TX | `rt-esp32/src/main.cpp` — steering LBS state machine | `test_components.cpp` — steering SM test | Code review | Verified |

### HAZ-04: Loss of Steering Control

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-04: Actuator comms loss must be detected within 100ms | FR-14: RT must detect 0x201 EPS-C status timeout | TR-14: EPS-C timeout detection at 20ms threshold | `rt-esp32/src/steering_control.h` — EPS-C status timeout | `test_steering_control.cpp` — timeout test | Unit test | Verified |
| SG-04 | FR-15: SYS must detect 0x721 SEB status timeout | TR-15: SEB timeout at 100ms with DLC guard | `sys-esp32/src/main.cpp:243-244` — 0x721 DLC guard | `test_sys_can_dispatch.cpp` — DLC guard test | Unit test | Verified |
| SG-04 | FR-16: Rolling counter must detect frozen actuator | TR-16: EPS-C and SEB rolling counter freshness monitored | `rt-esp32/src/steering_control.h` — frozen rolling counter detection | `test_components.cpp:139-142` — rolling counter test | Unit test | Verified |

### HAZ-05: Rollover From Excessive Steering at Speed

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-03 (shared) | FR-17: Dynamic angle clamp must limit steering at high speed | TR-17: Max 5 deg at 25 km/h, 18 deg at 10 km/h, 40 deg at 2 km/h | `rt-esp32/src/physics_model.cpp:31` — speed-dependent threshold | `test_components.cpp:90-93` | HIL T1.3: Angle vs speed | Needs HIL |
| SG-03 | FR-18: ESTOP obstacle response must apply dynamic clamp to hold angle | TR-18: Hold angle clamped to speed-safe envelope before silent-stop | Emergency system doc Section 3.1 — two-tier ESTOP steering | Not yet in simulation test | Design review | Open |
| SG-03 | FR-19: ESTOP non-obstacle response must ramp steering to 0 deg at 20 deg/s | TR-19: Ramp to center before silent handoff to MANUAL | Emergency system doc Section 3.2 — non-obstacle ramp | Not yet in simulation test | Design review | Open |

### HAZ-06: Loss of CAN Communication

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-05: Node crash must be detected within 1500ms | FR-20: Heartbeat monitoring on all nodes | TR-20a: SYS monitors RT heartbeat (0x7FD) at 1000ms timeout | `sys-esp32/src/safety_monitor.cpp:28-46` — heartbeat monitoring | `test_safety_features.cpp:48-76` (S2) | Integration test T4 | Verified |
| SG-05 | FR-20 | TR-20b: RT monitors SYS heartbeat (0x7FE) at 200ms timeout | `rt-esp32/src/safety_monitor.h:91-109` — braketakeover on timeout | `test_safety_features.cpp:48-76` (S2) | Integration test T3 | Verified |
| SG-05 | FR-20 | TR-20c: RT monitors Host heartbeat (0x7FC) at 1500ms timeout | `rt-esp32/src/safety_monitor.h:91-109` — assisted stop | `test_safety_features.cpp:48-76` (S2) | Integration test T4 | Verified |
| SG-05 | FR-20 | TR-20d: Heartbeat includes health flags byte (DLC=2) | `sys-esp32/src/main.cpp:880-893` — HB TX with health flags | `test_components.cpp:242-244` — heartbeat frozen detection | Unit test | Verified |
| SG-05 | FR-21: CAN bus-off detection and auto-recovery | TR-21: TWAI and MCP2515 bus-off handling | `rt-esp32/src/can_health.h:28` — bus-off sends ESTOP | `test_can_bus_off.cpp` | Integration test | Verified |
| SG-05 | FR-22: Command staleness detection on 0x204 and 0x300 | TR-22a: RT watchdog detects stale 0x300 at 500ms | `rt-esp32/src/watchdog.h:6-11` — CmdWatchdog class | `test_safety_features.cpp:174-186` (S7) | Unit test | Verified |
| SG-05 | FR-22 | TR-22b: MTR detects stale 0x204 at 200ms | `mtr-stm32/src/main.cpp:189-195` — staleness check | `test_components.cpp:301-311` | Unit test | Verified |
| SG-05 | FR-22 | TR-22c: SYS detects stale 0x204 at 200ms with grace period | `sys-esp32/src/main.cpp:111-112` — staleness tracking | `test_sys_can_dispatch.cpp:40-47` | Unit test | Verified |

### HAZ-07: MCU Firmware Hang or Crash

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-07: System must default to safe state on power-up/reset | FR-23: External watchdog must reset MCU if toggle stops | TR-23a: TPS3850 on RT, toggled by `control_task` at 100Hz | `rt-esp32/src/main.cpp:313-316` — WDT GPIO toggle | `test_safety_features.cpp:358-375` (S17) — task watchdog | HIL T2.3: Oscilloscope probe | Needs HIL |
| SG-07 | FR-23 | TR-23b: TPS3850 on SYS, toggled by `safety_task` at 20Hz | `sys-esp32/src/wdt_toggle.h:2-11` — WdtToggle class | `test_task_watchdog.cpp:1-81` | HIL T2.3 | Needs HIL |
| SG-07 | FR-24: Firmware must default to MANUAL mode on boot | TR-24: Mode variable initializes to MANUAL | `sys-esp32/src/main.cpp` — mode initialization | `test_safety_features.cpp` — mode test | Code audit | Verified |
| SG-07 | FR-25: All GPIOs must be safe state during MCU reset | TR-25: DAC=0V, relays OFF on GPIO float | `docs/hardware-safety.md` — hardware fail-safe behavior | Power-on test | Design review | Open |
| SG-07 | FR-26: Per-task alive counters must detect task stalls | TR-26: `check_task_watchdog()` monitors alive counter increments | `sys-esp32/src/main.cpp:815-819` — task watchdog | `test_task_watchdog.cpp:1-81` | Unit test | Verified |

### HAZ-08: ESTOP Fails to Engage

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-08: ESTOP must propagate to all nodes within 50ms | FR-27: Physical ESTOP button must trigger on all MCUs | TR-27a: ESTOP NC wiring to SYS GPIO1 and MTR kEstopGpio | `sys-esp32/src/main.cpp:209` — ESTOP GPIO handler | `test_safety_features.cpp` — ESTOP test | HIL T3.2 | Needs HIL |
| SG-08 | FR-27 | TR-27b: CAN 0x001 forwarded on both buses with priority | `rt-esp32/src/can_dispatch.h:78` — priority forwarding | `test_gateway_forwarding.cpp:81-83` | Integration test | Verified |
| SG-08 | FR-28: ESTOP must be rate-limited to prevent bus flooding | TR-28: Max 2 ESTOP frames per 1000ms window | `sys-esp32/src/config.h:93-95` — rate limit constants | `test_sys_qa.cpp:51-71` — 3rd frame dropped | Unit test | Verified |
| SG-08 | FR-29: ESTOP must be an absorbing state (power-cycle to exit) | TR-29: mode_set(Estop), only START or MODE long-press exits | `sys-esp32/src/main.cpp` — mode state machine | `test_safety_features.cpp` — mode transitions | Design review | Verified |
| SG-08 | FR-30: START button health monitored (stuck button detection) | TR-30: Diag task checks if ESTOP >30s with no START activity | `sys-esp32/src/main.cpp` — diag task | Proposed | Design review | Open |

### HAZ-09: Mechanical Steering Linkage Jam

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-03 (shared) | FR-31: Steering following error must trigger ESTOP on jam | TR-31: Compare commanded vs actual angle, ESTOP if error >threshold >300ms | `rt-esp32/src/safety_monitor.h:112-128` — following error check | `test_safety_features.cpp:128-140` (S5) | Unit test | Verified |
| SG-03 | FR-32: Following error threshold must be speed-scaled | TR-32: Threshold = max(2 deg, 0.25 x dynamic_limit) | `rt-esp32/src/physics_model.h:49-50` — speed-dependent threshold | `test_components.cpp:90-93` — threshold test | Unit test | Verified |

### HAZ-10: Brake Actuator (SEB) Communication Loss

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-04 (shared) | FR-33: SEB status (0x721) must be validated with DLC and checksum | TR-33: DLC<8 drop, XOR checksum validation | `shared/can/can_protocol.h:653` — SebStatus DLC guard | `test_dlc_consistency.cpp:1-109` — DLC tests | Unit test | Verified |
| SG-04 | FR-34: 0x7B9 command must include rolling counter and checksum | TR-34: Rolling counter increment + XOR checksum | `sys-esp32/src/brake_control.h:134-135` — rolling counter implementation | `test_protocol_roundtrip.cpp:692-725` | Unit test | Verified |
| SG-02 (shared) | FR-35: SEB status includes alignment and error status | TR-35: `SebStatus` parses alignment_status and SEB_Error_Status | `shared/can/can_protocol.h:656` — frame parsing | `test_protocol_roundtrip.cpp` — roundtrip test | Unit test | Verified |

### HAZ-11: CAN Frame Corruption

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-06: Corrupted CAN frames must be rejected | FR-36: XOR checksum on steer-by-wire frames (0x169, 0x201, 0x721, 0x7B9) | TR-36: XOR(bytes[0..6]) ^ 0xFF, validated on RX | `shared/can/can_protocol.h` — checksum fields | `test_checksum_full.cpp:397-420` — corruption detection | Unit test | Verified |
| SG-06 | FR-37: DLC must match protocol expectation for every frame | TR-37: DLC guard in every `from_frame()` method | `shared/can/can_protocol.h` — DLC guards on all frame types | `test_dlc_consistency.cpp:1-109` | Unit test | Verified |
| SG-06 | FR-38: Rolling counter must increment monotonically | TR-38: `(prev + 1) & 0x0F`, frozen counter = comms failure | `sys-esp32/src/brake_control.h:134-135` — counter increment | `test_protocol_roundtrip.cpp:676-687` | Unit test | Verified |

### HAZ-12: Debug Tool Zombie Connections

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-QM: Debug tool must not accumulate resources | FR-39: WebSocket ping/pong with stale client eviction | TR-39: 30s ping interval, 60s pong timeout, terminate stale clients | `debug-tool/backend/src/ws/stream.ts:70-80` — zombie eviction | Bug 9.1 test | Code review, system test | Verified |
| SG-QM | FR-40: WebSocket close handler must clean up resources | TR-40: Remove client from set on close event | `debug-tool/backend/src/ws/stream.ts:50-52` — close handler | Bug 9.2-9.4 tests | Code review | Verified |
| SG-QM | FR-41: API must guard ESTOP injection with confirmation | TR-41: `confirm_estop=true` required for 0x001 injection | `debug-tool/backend/src/api/cmd.ts:47` — API guard | Bug 9.3 test | Code review | Verified |

### HAZ-13: Dual CAN Sender Collision on 0x7B9

| Safety Goal | Functional Req | Technical Req | Implementation | Test | Verification Evidence | Status |
|-------------|---------------|---------------|---------------|------|---------------------|--------|
| SG-02 (shared) | FR-42: Only one node may transmit 0x7B9 at a time | TR-42a: RT suppresses 0x7B9 in ESTOP/FAULT; sends only in AUTO | `rt-esp32/src/main.cpp:439-448` — suppression logic | Bug 4.1/4.2 test: `test_qa_bugs.cpp` | Unit test | Verified |
| SG-02 | FR-42 | TR-42b: SYS resumes 0x7B9 in MANUAL, ESTOP, rider override | `sys-esp32/src/main.cpp:628` — SYS own-0x7B9 resumed | Bug 4.1/4.2 test | Unit test | Verified |

---

## Safety Mechanisms Summary

| Mechanism | Description | CAN IDs | Source File(s) | Verification |
|-----------|-------------|---------|----------------|--------------|
| **Heartbeat monitoring** | Alive counter + health flags on each node, cross-monitored | 0x7FC (Host), 0x7FD (RT), 0x7FE (SYS) | `sys-esp32/src/safety_monitor.cpp`, `rt-esp32/src/safety_monitor.h` | `test_safety_features.cpp:48-76` (S2) |
| **Command staleness** | Timestamp-based timeout on drive commands | 0x204 (200ms), 0x300 (500ms) | `rt-esp32/src/watchdog.h`, `mtr-stm32/src/main.cpp:189-195` | `test_safety_features.cpp:174-186` (S7) |
| **Steering following error** | Speed-scaled error threshold with 300ms persistence | 0x169 (cmd), 0x201 (actual) | `rt-esp32/src/safety_monitor.h:112-128` | `test_safety_features.cpp:128-140` (S5) |
| **ESTOP propagation** | CAN 0x001 with priority forwarding across both buses | 0x001 | `rt-esp32/src/can_dispatch.h:78`, `rt-esp32/src/can_dispatch.h:214-236` | `test_gateway_forwarding.cpp:81-83` |
| **Checksum validation** | XOR checksum on all steer-by-wire frames | 0x169, 0x201, 0x721, 0x7B9 | `shared/can/can_protocol.h` DLC guards + XOR fields | `test_checksum_full.cpp:397-420` |
| **Rolling counter** | 4-bit counter guards against replay/stale frames | 0x169, 0x201, 0x721, 0x7B9 | `sys-esp32/src/brake_control.h:134-135` | `test_protocol_roundtrip.cpp:676-687` |
| **EGAS L2 speed monitoring** | SYS compares 0x204 setpoint vs 0x206 actual speed | 0x204, 0x206 | `sys-esp32/src/main.cpp:428-457` | `test_safety_features.cpp:154-170` (S6) |
| **DLC validation guards** | All `from_frame()` methods reject wrong DLC | All frames | `shared/can/can_protocol.h` (all frame types) | `test_dlc_consistency.cpp:1-109` |
| **External watchdog** | TPS3850 IC, independent RC oscillator, 100ms timeout | N/A (GPIO) | `rt-esp32/src/main.cpp:313-316`, `sys-esp32/src/wdt_toggle.h` | `test_task_watchdog.cpp:1-81` |
| **Internal task watchdog** | Per-task alive counters, task stall detection | N/A | `sys-esp32/src/main.cpp:815-819` | `test_safety_features.cpp:358-375` (S17) |
| **Dynamic angle clamp** | Speed-inverse proportional steering limit | 0x169 (target angle) | `rt-esp32/src/physics_model.cpp:31` | `test_components.cpp:90-93` |
| **Software hard-stops** | Absolute +/-40 deg clamp on all steering commands | 0x169 | `rt-esp32/src/main.cpp` — clamp before TX | Code audit |
| **Listen Before Speaking** | Boot-time sync to actuator position before transmission | 0x169, 0x201, 0x7B9, 0x721 | `rt-esp32/src/main.cpp` (steering LBS), `sys-esp32/src/brake_control.h` (brake LBS) | `test_steering_control.cpp` |

---

## Bug Fix Traceability

| Bug ID | Description | Fix Location | Hazard | Safety Goal | Status |
|--------|-------------|-------------|-------|-------------|--------|
| 4.1 | Brake zeroing — dual sender collision on 0x7B9 | `rt-esp32/src/main.cpp:439-448` | HAZ-13 | SG-02 | Verified |
| 4.2 | Brake zeroing — RT 0x7B9 overriding SYS on ESTOP | `rt-esp32/src/main.cpp:439-448` | HAZ-13 | SG-02 | Verified |
| 4.10 | SEB alignment bit uninitialized (align_enable=0) | `sys-esp32/src/brake_control.h:72` | HAZ-02 | SG-02 | Verified |
| 5.3 | ESTOP rate-limit missing in SYS dispatch | `sys-esp32/src/main.cpp:218-232` | HAZ-08 | SG-08 | Verified |
| 6.1 | ESTOP queue bypass timeout in CAN dispatch | `sys-esp32/src/main.cpp` | HAZ-08 | SG-08 | Verified |
| 6.2 | ECU temp underflow in diagnostics | `sys-esp32/src/main.cpp` | QM | — | Verified |
| 6.3 | ESTOP rate-limiting not applied to TX path | `sys-esp32/src/config.h:93-95` | HAZ-08 | SG-08 | Verified |
| 7.1 | Physics model following error threshold off-by-one | `rt-esp32/src/physics_model.h:49-50` | HAZ-09 | SG-03 | Verified |
| 7.2 | SPI bus-off recovery missing retry | `rt-esp32/src/can_driver_mcp2515.cpp:371,380` | HAZ-06 | SG-05 | Verified |
| 7.3 | Steering checksum validation not rejecting corrupted frames | `rt-esp32/src/can_dispatch.h:160-161` | HAZ-11 | SG-06 | Verified |
| 7.4 | Brake 0x7B9 checksum not validated on RX | `rt-esp32/src/main.cpp` | HAZ-11 | SG-06 | Verified |
| 7.5 | EPS-C 0x201 rolling counter not checked for frozen counter | `rt-esp32/src/steering_control.h` | HAZ-04 | SG-04 | Verified |
| 8.1 | MCP4725 I2C infinite timeout (HAL_MAX_DELAY) | `mtr-stm32/src/mcp4725_dac.h:29-30` | HAZ-01 | SG-01 | Verified |
| 8.2 | I2C deadlock blocks DAC write, defeating HW ESTOP | `mtr-stm32/src/mcp4725_dac.h:37-43` | HAZ-01, HAZ-07 | SG-01 | Verified |
| 8.3 | MTR task watchdog not monitoring all tasks | `mtr-stm32/src/main.cpp` | HAZ-07 | SG-07 | Verified |
| 8.4 | MTR gear relay stuck-on on cold boot | `mtr-stm32/src/main.cpp` | HAZ-01 | SG-01 | Verified |
| 8.5 | MTR 0x204 staleness not triggering in all modes | `mtr-stm32/src/main.cpp:189-195` | HAZ-01 | SG-01 | Verified |
| 9.1 | Zombie WebSocket connections accumulate memory | `debug-tool/backend/src/ws/stream.ts:70-80` | HAZ-12 | SG-QM | Verified |
| 9.2 | WebSocket close handler leaks client references | `debug-tool/backend/src/ws/stream.ts:50-52` | HAZ-12 | SG-QM | Verified |
| 9.3 | API allows ESTOP injection without confirmation | `debug-tool/backend/src/api/cmd.ts:47` | HAZ-12 | SG-QM | Verified |
| 9.4 | Stream broadcast crashes on undefined readyState | `debug-tool/backend/src/ws/stream.ts:87-89` | HAZ-12 | SG-QM | Verified |
| D2 | CAN DLC generation ignoring explicit dlc: fields | `shared/can/can_protocol.h` DLC values fix | HAZ-11 | SG-06 | Verified |

---

## Commented Bug References in Source

| Bug | Source File | Comment |
|-----|-------------|---------|
| 4.1, 4.2 | `rt-esp32/src/main.cpp:441` | `// bus collision and brake=0 override (bugs 4.1, 4.2)` |
| 4.10 | `rt-esp32/test/.stale/test_qa_bugs.cpp:52-77` | Reproduction test for SEB alignment bit |
| 5.3 | `sys-esp32/src/test_sys_qa.cpp:51-71` | ESTOP rate-limit verification |
| 6.3 | `sys-esp32/src/main.cpp:114` | `// Gap #14: Rate-limit 0x001 ESTOP broadcasts` |
| 7.5 | N/A — frozen rolling counter guard added per commit `cd9209b` | `Add frozen rolling-counter guard for EPS-C 0x201 feedback` |
| 8.2 | `mtr-stm32/src/mcp4725_dac.h:29-30` | `// HAL_MAX_DELAY would block forever on I2C bus disruption, defeating hardware ESTOP (bug 8.2)` |
| 9.1 | `debug-tool/backend/src/ws/stream.ts:70` | `// Evict clients with no pong response within 60s (zombie connection guard)` |
| D2 | `shared/can/can_protocol.h` | CAN DLC generation fix per YAML explicit dlc field |

---

## Test Coverage Summary

| Test File | Tests | Coverage |
|-----------|-------|----------|
| `native-test/test/test_safety_features.cpp` | S1-S17 | Heartbeat, ESTOP, staleness, following error, EGAS, task watchdog |
| `native-test/test/test_components.cpp` | 20+ tests | Following error threshold, rolling counter, heartbeat frozen, cmd staleness |
| `native-test/test/test_dlc_consistency.cpp` | Full scan | All CAN IDs verified against expected DLC |
| `native-test/test/test_checksum_full.cpp` | Section 1-7 | XOR invariant, corruption detection, rolling counter checksum |
| `native-test/test/test_protocol_roundtrip.cpp` | 20+ tests | Signal encode/decode, checksum, rolling counter |
| `native-test/test/test_gateway_forwarding.cpp` | 10 tests | ESTOP forwarding, message routing |
| `native-test/test/test_task_watchdog.cpp` | 81 lines | Multi-task watchdog stall detection |
| `native-test/test/test_sys_can_dispatch.cpp` | 10+ tests | DLC guard, 0x204 staleness |
| `sys-esp32/test/test_brake_priority.cpp` | Rolling counter | Brake control rolling counter test |
| `sys-esp32/test/test_sys_qa.cpp` | QA bugs 5.3 | ESTOP rate-limiting |
| `rt-esp32/test/test_all_signals_native.cpp` | Signal isolation | All signals decode/encode, DLC guard |

---

## HIL Test Plan Status

| Tier | Tests | Scope | Status |
|------|-------|-------|--------|
| Tier 1 | T1.1-T1.7 | Steering angle offset, SEB stroke, timeout behavior, power sequencing | Not tested |
| Tier 2 | T2.1-T2.7 | ESTOP, brake lever, heartbeat loss, stale cmd, following error, angle clamp, CAN bus-off | Not tested |
| Tier 3 | T3.1-T3.4 | SEB comm loss, motor fault, power sequencing, brownout | Not tested |
| Tier 4 | T4.1-T4.3 | Autoware bridge, debug tool, soak test | Not tested |

> **Note:** All HIL tests remain "Not tested" pending hardware availability. Unit and integration tests provide pre-HIL verification.

---

*Reference documents: [defense-in-depth-safety](defense-in-depth-safety.md), [emergency-system](emergency-system.md), [external-watchdog](external-watchdog.md), [hardware-safety](hardware-safety.md), [listen-before-speaking](listen-before-speaking.md), [hil-safety-test-plan](hil-safety-test-plan.md), [distributed-architecture](distributed-architecture.md), [architecture-reference](./architecture-reference.md)*
