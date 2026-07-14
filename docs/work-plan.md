# E-Trike Architecture Remediation Plan

Goal: bring the codebase into alignment with [`architecture.md`](architecture.md).  
The analysis (2026-06-14) found that the codebase diverged from the architecture across three axes:
1. **Transport layer**: code uses a UART inter-MCU link; architecture specifies CAN.
2. **Unimplemented modules**: CAN gateway, steering control, brake control, and 15 SYS tasks are stubs or disconnected.
3. **Symbol errors**: ~20 missing constants, wrong type names, and conflicting struct definitions.

Each phase delivers compilable code with a runnable test. Phases are ordered by dependency.

---

## Phase R1 — Fix shared CAN protocol (no regressions)

**Goal:** Resolve every type-name mismatch and missing constant in `protocol/generated/cpp/protocol.h`. All existing tests must pass with zero changes. New tests must pass.

**Files:**
- `protocol/generated/cpp/protocol.h`

**Changes:**

| # | Problem | Fix |
|---|---------|-----|
| R1.1 | `kIdHostBrakeReq` — code uses `kIdHostBrakeRequest` | Add `constexpr auto kIdHostBrakeRequest = kIdHostBrakeReq;` alias |
| R1.2 | `HostBrakeReq` — code uses `HostBrakeRequest` | Add `using HostBrakeRequest = HostBrakeReq;` alias |
| R1.3 | `RtObstacleRpt` — code uses `RtObstacleDist` | Add `using RtObstacleDist = RtObstacleRpt;` alias |
| R1.4 | `SysDiagRpt` — code uses `SysDiag` | Add `using SysDiag = SysDiagRpt;` alias |
| R1.5 | `kIdSbwStatus`, `kIdBbwStatus` — code uses `kIdSbwStatus`, `kIdBbwStatus` | Add aliases |
| R1.6 | No heartbeat ID constant — code uses `kIdHeartbeat` | Add `constexpr uint32_t kIdHeartbeat = 0x7FD;` alias |
| R1.7 | `0x300 HostDriveCmd` missing gear field (architecture gap #2 resolved) | Add `uint8_t gear = 0;` field at byte 7 (repack DLC stays 8, yaw becomes i24 in bytes 4-6) |
| R1.8 | No ESTOP sub-IDs (`kIdSysEstop`, `kIdRtEstop`, `kIdHostEstop`) — code in `can_rx_router.h` uses them | Add `constexpr` aliases all pointing to `kIdSafetyEstop` |

**Acceptance:**
```
cd rt-esp32/test
g++ -std=c++17 -I../../shared test_can_protocol.cpp -o test_can && ./test_can
# All tests pass. 0 failures.
```

---

## Phase R2 — Fix RT config header

**Goal:** Add every missing constant that RT code references. File must compile standalone.

**Files:**
- `rt-esp32/src/config.h`

**Changes:**

| # | Missing constant | Value / source |
|---|-----------------|----------------|
| R2.1 | `kPidKp`, `kPidKi`, `kPidKd` | Uncomment the existing line: `constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f;` |
| R2.2 | `kInterMcuUartPort` | This is a **design error** — RT should not use UART to SYS. Remove the dependency instead. See Phase R4. For now: add with a `// REMOVE in Phase R5` comment so the file compiles. |
| R2.3 | `kInterMcuTxGpio` | Ditto — `constexpr int kInterMcuTxGpio = 17;` with deprecation comment |
| R2.4 | `kInterMcuRxGpio` | Ditto — `constexpr int kInterMcuRxGpio = 18;` with deprecation comment |
| R2.5 | `kInterMcuBaud` | Ditto — `constexpr int kInterMcuBaud = 2'000'000;` with deprecation comment |
| R2.6 | `kSteerLimitDeg` (used in `physics_model.cpp` but only `kSteerHardLimitDeg` exists) | Add `constexpr float kSteerLimitDeg = 40.0f;` |

**Acceptance:**
```
cd rt-esp32/test
g++ -std=c++17 -I../../shared -I../src test_rt_config.cpp -o test_config && ./test_config
# Compiles. All constants resolve.
```

---

## Phase R3 — Fix SYS config header

**Goal:** Add every missing constant that SYS code references.

**Files:**
- `sys-esp32/src/config.h`

**Changes:**

| # | Missing constant | Value / source |
|---|-----------------|----------------|
| R3.1 | `kBrakeGpio` (used by `brake_actuator.cpp`) | This is a **design error** — brake is SEB over CAN `0x7B9`, not a GPIO solenoid. Add `constexpr int kBrakeGpio = 0; // DEPRECATED — remove in Phase R8` so it compiles. |
| R3.2 | `kMotorPwmGpio` (used by `motor_driver.cpp`) | `constexpr int kMotorPwmGpio = 0;` — MCP4725 DAC is the correct actuator per architecture. Add deprecation. |
| R3.3 | `kMotorDirGpio` | `constexpr int kMotorDirGpio = 0;` — deprecation |
| R3.4 | `kMotorPwmFreqHz` | `constexpr int kMotorPwmFreqHz = 20000;` — deprecation |
| R3.5 | `kMotorMaxSpeedMmps` | `constexpr int kMotorMaxSpeedMmps = 3000;` |
| R3.6 | `kPwmMax` | `constexpr int kPwmMax = 8191;` — deprecation |
| R3.7 | `kObstacleStopDistMM` | `constexpr unsigned kObstacleStopDistMM = 300;` |
| R3.8 | `kObstacleClearDistMM` | `constexpr unsigned kObstacleClearDistMM = 3000;` |

**Acceptance:**
```
# Host-compile SYS config with a trivial main
g++ -std=c++17 -I../../shared -I../src -c -x c++ ../src/config.h -o /dev/null
# No undefined symbols.
```

---

## Phase R4 — Eliminate the UART inter-MCU abstraction (RT side) ✅ COMPLETED

**Goal:** All inter-MCU communication between RT and SYS moves to low-level CAN, matching architecture §7.3 and §8.3. The `inter_mcu` UART protocol is removed from RT.

**This is the pivot phase.** It replaces the architectural violation at the root.

**Files:**
- `rt-esp32/src/main.cpp` — rewrite communication paths
- `rt-esp32/src/control_logic.h` — change return type from `inter_mcu::RtToSysSetpoint` to the CAN-based structs
- `rt-esp32/src/control_logic.cpp` — build `can::RtDriveCmd` + `can::RtBrakeCmd` instead of UART frame
- Delete: `#include "intermcu/intermcu_protocol.h"` and `#include "intermcu/intermcu_driver.h"` from all RT files

**What changes in `main.cpp`:**

```
BEFORE (UART):                    AFTER (CAN):
┌──────────────────────┐          ┌──────────────────────────┐
│ control_task         │          │ control_task (100 Hz)    │
│  resolve → UART setpoint │      │  resolve → can::RtDriveCmd│
│  push UART queue     │          │  push can_tx_low_queue   │
│ link_tx_task         │          │  resolve → can::RtBrakeCmd│
│  send UART frame     │          │  push can_tx_low_queue   │
│ link_rx_task         │          │                          │
│  receive UART status │          │ can_tx_low_task (prio 3) │
└──────────────────────┘          │  send 0x204 @ 100 Hz    │
                                  │  send 0x205 @ 50 Hz     │
                                  │  send 0x169 @ 50 Hz     │
                                  │ can_rx_low_task (prio 5)│
                                  │  receive 0x110, 0x011,  │
                                  │  0x120, 0x201, 0x600,   │
                                  │  0x7FE → dispatch       │
                                  └──────────────────────────┘
```

**Changed struct/return:**

`control_logic.h`:
```cpp
struct ControlOutput {
    can::RtDriveCmd drive;    // → 0x204
    can::RtBrakeCmd brake;    // → 0x205
    // 0x169 steering is generated by steering_control, not here
};
ControlOutput resolve_drive_setpoint(…);
```

**Acceptance (host test):**
```
cd rt-esp32/test
g++ -std=c++17 -I../../shared -I../src test_control_logic.cpp ../src/control_logic.cpp ../src/physics_model.cpp ../src/speed_pid.cpp -o test_ctl && ./test_ctl
# Known inputs → correct can::RtDriveCmd + can::RtBrakeCmd output. No intermcu headers included.
```

---

## Phase R5 — Implement RT dual-CAN driver ✅ COMPLETED

**Goal:** RT has two working CAN interfaces: built-in TWAI (low bus) and MCP2515 via SPI (high bus). Matches architecture §7.2.

**Files:**
- `rt-esp32/src/can_driver_twai.cpp` — replace empty stub with real TWAI init from `protocol/contracts/can_driver.h`
- `rt-esp32/src/can_driver_mcp2515.cpp` — implement MCP2515 init/read/write over SPI
- `rt-esp32/src/can_driver_mcp2515.h` — keep the MCP2515 class interface
- `rt-esp32/src/main.cpp` — create two `CanDriver` instances (or one TWAI + one MCP2515)

**TWAI (low bus):** Reuse `can::CanDriver` from shared. Config: TX=5, RX=4, 500 kbit/s.

**MCP2515 (high bus):** SPI at 10 MHz on SCK=15, MOSI=16, MISO=17, CS=18, INT=47. Implement:
- `mcp2515_init()`: reset, set bitrate, normal mode, configure RX buffers + interrupts
- `mcp2515_send(const can::Frame&)`: load TX buffer, request send
- `mcp2515_receive(can::Frame&, timeout)`: read RX buffer on INT or poll

**Acceptance (hardware loopback test):**
```
# ESP32 + MCP2515 module on breadboard. TX→RX loopback (120Ω terminated).
# Send 1000 frames on each interface. Verify all received. 0 TEC/REC.
```

---

## Phase R6 — Implement RT CAN gateway (dispatch + forwarding) ✅ COMPLETED

**Goal:** RT correctly forwards, translates, and processes CAN frames per architecture §2.3 Categories 1–3.

**Files:**
- `rt-esp32/src/main.cpp` — rewrite `dispatch_task`
- `rt-esp32/src/can_rx_router.h` — update `route_frame()` to use real queues, remove `GatewayQueues` raw-pointer struct

**Implementation:**

```
dispatch_task @ prio 4:
  Block on can_rx_low_queue OR can_rx_high_queue

  Category 1 — Transparent forward:
    0x001 (any bus)   → forward to other bus + mode_set(Estop)
    0x011 (low)       → forward to high (gw_tx_high_queue)
    0x120 (low)       → forward to high
    0x600 (low)       → forward to high
    0x302 (high)      → forward to low (gw_tx_low_queue)

  Category 2 — Consume + generate:
    0x300 (high)      → HostDriveCmd::from_frame → cmd_queue (overwrite)
    0x301 (high)      → g_brake_request_kpa atomic store

  Category 3 — Consume only:
    0x110 (low)       → mode_set(Manual/Auto)
    0x201 (low)       → steer_feedback queue (for steering SM)
    0x7FE (low)       → feed SYS heartbeat alive counter
    0x7FC (high)      → feed Jetson heartbeat alive counter
```

**Acceptance (host test):**
```
cd rt-esp32/test
g++ -std=c++17 -I../../shared -I../src test_rt_full.cpp -o test_rt && ./test_rt
# Inject CAN frames on both buses. Verify:
#  - 0x011 on low → appears on gw_tx_high queue
#  - 0x302 on high → appears on gw_tx_low queue
#  - 0x300 on high → cmd_queue receives HostDriveCmd
#  - 0x110 on low → mode changes to AUTO
#  - 0x001 on either bus → ESTOP propagated to other bus
#  - 0x201 on low → steer_feedback_queue receives angle
```

---

## Phase R7 — Implement RT CAN TX tasks ✅ COMPLETED

**Goal:** RT transmits all required CAN frames at correct rates on both buses.

**Files:**
- `rt-esp32/src/main.cpp` — add `can_tx_low_task`, `can_tx_high_task`
- `rt-esp32/src/steering_control.cpp` — implement (replacing header-only stub)
- `rt-esp32/src/heartbeat.cpp` — implement (replacing header-only stub)

**can_tx_low_task @ prio 3:**
| Frame | Rate | Source |
|-------|------|--------|
| `0x204 RT_DRIVE_CMD` | 100 Hz | `setpoint_queue` (from control_task) |
| `0x205 RT_BRAKE_CMD` | 50 Hz | `brake_queue` (from control_task) |
| `0x169 VCU_SES_REQ` | 50 Hz | steering state machine (STEER_ACTIVE only) |
| `0x302 HOST_LIGHT_CMD` | On change | `gw_tx_low_queue` |
| `0x001 SAFETY_ESTOP` | Event | direct TX (bypasses queues per principle #2) |

**can_tx_high_task @ prio 3:**
| Frame | Rate | Source |
|-------|------|--------|
| `0x011 SYS_SAFETY_STS` | 5 Hz | `gw_tx_high_queue` (forwarded from SYS) |
| `0x120 SYS_THROTTLE_STS` | 100 Hz | `gw_tx_high_queue` |
| `0x210 RT_STATE_RPT` | 10 Hz | g_mode + safety_state/estop_reason + reversing |
| `0x220 RT_PID_RPT` | — | Inactive until encoders fitted |
| `0x400 RT_OBSTACLE_RPT` | 10 Hz | g_obstacle_mm |
| `0x600 SYS_DIAG_RPT` | 1 Hz | `gw_tx_high_queue` (forwarded from SYS) |

**Acceptance (ESP32):**
```
# Logic analyzer on low CAN: 0x204 every 10ms, 0x169 every 20ms (when steer active).
# Logic analyzer on high CAN: 0x120 every 10ms, 0x210 every 100ms.
# Both buses simultaneously active. TEC/REC = 0 after 10 min.
```

---

## Phase R8 — Wire RT steering + heartbeat to CAN tasks ✅ COMPLETED

**Goal:** The steering `Listen-Before-Speaking` state machine runs and transmits `0x169`. Heartbeat transmits `0x7FD` on both buses with alive counter.

**Files:**
- `rt-esp32/src/steering_control.cpp` — real implementation from header-only stub
- `rt-esp32/src/heartbeat.cpp` — real implementation with alive counter validation

**Steering control:**
```
steering_task @ prio 3, 50 Hz:
  Switch on SteerState:
    BOOT_WAIT:   500ms delay → LISTEN_SYNC
    LISTEN_SYNC: Wait for 0x201 SES_STATUS → set active_target = current_angle → ACTIVE
    ACTIVE:      Build VcuSesReq with dynamic clamp, slew rate, rolling counter, checksum
                 → push to can_tx_low for 0x169 transmission
    FAULT:       Stop transmitting
```

**Heartbeat:**
```
heartbeat_task @ 2 Hz:
  Send 0x7FD on low CAN: DLC=2, alive_ctr_low++, health_flags byte1
  Send 0x7FD on high CAN: DLC=2, alive_ctr_high++, health_flags byte1  (independent counters per bus)
  
  Receive monitoring:
    0x7FE (SYS, low):  detect frozen counter → ESTOP after 1000ms
    0x7FC (Jetson, high): detect frozen counter → zero setpoints after 1500ms
```

**Acceptance (ESP32):**
```
# Low CAN: 0x7FD every 500ms, counter increments per frame.
# High CAN: 0x7FD every 500ms, independent counter.
# Stop SYS → RT detects 0x7FE frozen → ESTOP after 1000ms.
# Stop Jetson → RT detects 0x7FC frozen → zero 0x204 after 1500ms.
```

---

## Phase S1 — Wire SYS CAN RX task (real CAN, not stubs)

**Goal:** SYS receives CAN frames on low bus and routes them to the correct consumers using the existing `can_dispatch.h` logic.

**Files:**
- `sys-esp32/src/main.cpp` — replace `task_can_rx` stub with real TWAI receive loop

**Implementation:**
```cpp
void task_can_rx(void*) {
    can::CanDriver can({.tx_gpio = 5, .rx_gpio = 4, .bitrate_hz = 500'000});
    can.init();
    can::Frame fr;
    while (1) {
        if (can.receive(fr, 100)) {
            xQueueSend(g_can_rx_queue, &fr, 0);  // drop if full
        }
    }
}
```

**Acceptance (ESP32):**
```
# Send 0x204 from another node → frame appears in g_can_rx_queue.
# Queue full → frame dropped (no crash).
```

---

## Phase S2 — Wire SYS dispatch task

**Goal:** SYS dispatch reads CAN RX queue and routes frames using the existing `DispatchTargets` struct.

**Files:**
- `sys-esp32/src/main.cpp` — replace `task_dispatch` stub

**Implementation:**
```cpp
void task_dispatch(void*) {
    can::Frame fr;
    while (1) {
        if (xQueueReceive(g_can_rx_queue, &fr, portMAX_DELAY) != pdTRUE) continue;
        DispatchTargets t;
        t.setpoint = &g_setpoint;
        t.brake_kpa = &g_brake_kpa;
        t.light_bits = &g_light_bits;
        t.estop_flag = &g_estop_flag;
        t.seb_status_raw = g_seb_status_raw;
        t.rt_hb_ctr = &g_rt_hb_ctr;
        t.rt_hb_received = &g_rt_hb_received;
        dispatch_frame(fr, t);
        if (g_estop_flag) mode_manager.force_estop();
    }
}
```

**Acceptance (host test):**
```
# Inject 0x204 → g_setpoint receives RtDriveCmd.
# Inject 0x205 → g_brake_kpa receives pressure value.
# Inject 0x001 → g_estop_flag set, mode transitions to ESTOP.
# Inject 0x302 → g_light_bits updated.
# Inject 0x721 → g_seb_status_raw populated.
# Inject 0x7FD → g_rt_hb_ctr updated, g_rt_hb_received = true.
```

---

## Phase S3 — Wire SYS motor task (MCP4725 DAC + gear relays)

**Goal:** `motor_task` at 100 Hz reads the drive setpoint and controls the MCP4725 DAC + gear relays per architecture §8.6.

**Files:**
- `sys-esp32/src/main.cpp` — replace `task_motor` stub

**Implementation:**
```
motor_task @ 100 Hz, prio 4:
  if mode == ESTOP:
      MCP4725 = 0, gear relays = all OFF
  elif mode == MANUAL:
      ADC read → MCP4725 pass-through
      TLP281 gear sense → mirror to relays
  elif mode == AUTO:
      setpoint.speed → abs(speed)/3000 * 4095 → MCP4725
      setpoint.gear → energize relay
  Also check: 0x204 staleness > 200ms → zero speed + N
```

**Acceptance (ESP32 + multimeter):**
```
# MANUAL: twist throttle grip → MCP4725 VOUT tracks ADC linearly.
# AUTO: inject 0x204 {speed=2000, gear=D} → MCP4725 ≈ 3.3V, gear D relay energized.
# ESTOP: MCP4725 = 0V, all relays OFF.
```

---

## Phase S4 — Wire SYS safety, mode, throttle, brake tasks

**Goal:** All remaining SYS tasks are wired to their implementation modules. This is a bulk wiring phase — each task is straightforward.

**Files:**
- `sys-esp32/src/main.cpp` — replace stubs for: `task_safety`, `task_mode`, `task_throttle`, `task_brake`, `task_lights`, `task_dcdc`, `task_indicator`, `task_power`, `task_can_tx`, `task_diag`, `task_hb`

**Each task:**

| Task | Rate | What it does |
|------|------|-------------|
| `safety` | 20 Hz | Poll GPIO1 (ESTOP), GPIO2 (brake lever). Check `SafetyMonitor::heartbeat_ok()`. If ESTOP or HB timeout → `mode_manager.force_estop()`. Toggle WDT GPIO47. |
| `mode` | 10 Hz | Read GPIO11 (MODE btn), GPIO41 (START btn). Call `ModeManager::tick()`. On change → send `0x110 SYS_MODE_CMD`. |
| `throttle` | 100 Hz | ADC read → `ThrottleInput::poll()`. Send `0x120 SYS_THROTTLE_STS`. |
| `brake` | 50 Hz | Run `BrakeControl::tick()`. Build `0x7B9 VCU_SEB_REQ` with rolling counter + checksum. Send on CAN. |
| `lights` | 20 Hz | Read handlebar switches GPIO9/6/7. `LightControl::tick()` → update GPIO18-22. Handle blink timing. |
| `dcdc` | 5 Hz | `DcdcControl::tick(estop)` → send `0x012` on state change. |
| `indicator` | 5 Hz | `IndicatorControl::tick(mode)` → AUTO/MANUAL bulbs (GPIO48/26). |
| `power` | 5 Hz | 12V relay GPIO40: ON in MANUAL/AUTO, OFF in ESTOP. |
| `can_tx` | 5 Hz | Send `0x011 SYS_SAFETY_STS` (estop + hb_ok). |
| `diag` | 1 Hz | Collect TEC/REC, heap, mode, estop. Send `0x600 SYS_DIAG_RPT`. |
| `hb` | 2 Hz | Send `0x7FE SYS_HEARTBEAT`: DLC=2, `alive_ctr++ & 0xFF, health_flags`. |

**Acceptance (ESP32 + logic analyzer):**
```
# All 11 CAN messages appear on low bus at correct rates.
# GPIO outputs change with mode transitions.
# ESTOP button → all CAN messages stop except 0x011 (estop=1) and 0x7B9 (max stroke).
# START button → transitions to MANUAL, 0x110 sent.
```

---

## Phase S5 — Remove SYS brake_actuator (GPIO solenoid pattern)

**Goal:** Brake actuation is exclusively through brake-by-wire unit via CAN `0x7B9`. The legacy GPIO-based `BrakeActuator` class is removed.

**Files:**
- Delete: `sys-esp32/src/brake_actuator.cpp`, `sys-esp32/src/brake_actuator.h`
- `sys-esp32/src/config.h` — remove `kBrakeGpio`

**Acceptance:** Build succeeds. No references to `BrakeActuator` or `kBrakeGpio` remain.

---

## Phase S6 — Remove or isolate SYS motor_driver.cpp (PWM pattern)

**Goal:** Motor actuation is exclusively through MCP4725 DAC. The PWM-based `motor_driver.cpp` does not belong in the SYS architecture. Move it to a `legacy/` directory or delete it.

**Files:**
- Move `sys-esp32/src/motor_driver.cpp` → `legacy/sys-esp32/src/motor_driver.cpp`
- `sys-esp32/src/config.h` — remove `kMotorPwmGpio`, `kMotorDirGpio`, `kMotorPwmFreqHz`, `kPwmMax`

**Acceptance:** Build succeeds. `motor_driver.h` (the MCP4725-based header) is the only motor interface.

---

## Phase I1 — Integration: RT + SYS on low-level CAN

**Goal:** Both ESP32s on the same CAN bus. RT sends `0x204`, `0x205`, `0x7FD`. SYS sends `0x011`, `0x110`, `0x120`, `0x600`, `0x7B9`, `0x7FE`. Each node correctly receives and acts on the other's frames.

**Hardware:** Two ESP32-S3 devkits, one SN65HVD230 transceiver each, 120Ω termination at both ends.

**Test scenarios:**

| Test | RT action | SYS action | Verify |
|------|-----------|------------|--------|
| Mode toggle | — | MODE btn → `0x110` | RT mode changes to AUTO |
| Heartbeat | Send `0x7FD` | Monitor counter | SYS `heartbeat_ok()` stays true |
| HB loss | Stop RT | Counter frozen | SYS detects ESTOP after 1000ms |
| Drive command | `0x204 {1500, D}` on low | Receive + actuate | MCP4725 ≈ 2.5V, gear D relay on |
| Brake command | `0x205 {5000}` on low | Receive + forward | SYS sends `0x7B9` in Pressure Mode |
| ESTOP button | — | ESTOP pressed | `0x001` on bus, RT enters ESTOP |

**Acceptance:** All 6 scenarios pass. 30-minute soak: TEC/REC = 0, no queue overflows.

---

## Phase I2 — Integration: RT + SYS + Jetson (3-node)

**Goal:** Jetson sends `0x300`, `0x301`, `0x302` on high CAN. RT bridges `0x302` to low CAN, translates `0x300`→`0x204`+`0x169`. SYS actuates. Telemetry flows SYS→RT→Jetson.

**Test:** Jetson publishes `/cmd_vel {linear.x=1.5, angular.z=0.2}` → `0x300` on high CAN → RT receives, resolves kinematics → `0x204` on low CAN → SYS MCP4725 outputs correct voltage → EPS-C receives `0x169` angle command.

**Acceptance:** Full pipeline from ROS 2 topic to actuator output. End-to-end latency < 50ms.

---

## Phase I3 — Safety validation (all ESTOP paths)

**Goal:** Verify every ESTOP trigger in architecture §7.10 and §8.10.

| # | Trigger | Expected response |
|---|---------|------------------|
| 1 | Physical ESTOP button (GPIO1 LOW) | SYS: DAC=0, gears OFF, `0x7B9` max stroke, DCDC `0x012`=0, 12V relay OFF. `0x011` estop=1. |
| 2 | CAN `0x001` from Jetson | RT fwds to low → SYS enters ESTOP |
| 3 | CAN `0x001` from RT (SYS HB timeout) | SYS enters ESTOP |
| 4 | CAN `0x001` from SYS (RT HB timeout) | RT enters ESTOP |
| 5 | Steering following error >5° for 300ms | RT → ESTOP, `0x169` ramps to 0° |
| 6 | External watchdog timeout | MCU reset → all outputs safe state |

**Acceptance:** Scripted test harness injects each fault. All 6 paths trigger ESTOP. Each verified by measuring DAC voltage, gear relay state, and CAN frames.

---

## Phase I4 — Endurance soak

**Goal:** 4-hour continuous run with all 3 nodes. AUTO mode cycling: speed 0→2000→0 mm/s, steering weave ±10°, periodic brake at 5000 kPa.

**Monitoring:**
- Stack high-water marks (all tasks < 80% allocation)
- Heap free (stable within ±2 KB)
- TEC/REC (0 for entire run)
- CAN frame rates (no gaps > 2x period)
- Queue depths (never at capacity)
- External watchdog never fires

**Acceptance:** Zero crashes. Zero CAN bus errors. Zero watchdog resets. All CAN frames at correct rates throughout.

---

## Dependency graph

```
R1 (protocol) ─────────────────────────────────────────────────────────────┐
    │                                                                       │
    ├── R2 (RT config) ── R5 (dual CAN) ── R6 (gateway) ── R7 (TX tasks) ──┤
    │                                     ── R8 (steer+hb)                  │
    │                                                                       │
    ├── R3 (SYS config) ────────────────────────────────────────────────────┤
    │                                                                       │
    └── R4 (remove UART) ───────────────────────────────────────────────────┤
                                                                             │
S1 (SYS CAN RX) ── S2 (dispatch) ── S3 (motor) ── S4 (all tasks)           │
    └── S5 (remove GPIO brake) ── S6 (remove PWM motor)                     │
                                                                             │
                          ┌────── I1 (RT+SYS CAN) ──────┐                   │
                          │                              │                   │
All above ────────────────┤                              ├── I3 (safety)     │
                          │                              │                   │
                          └────── I2 (3-node) ───────────┴── I4 (soak)      │
```

**Parallel work possible:** R2+R3 can be done in parallel. S1–S4 can start after R1+R3. R5–R8 depend on R2+R4.

---

## File inventory — what changes

| File | Phase | Action |
|------|-------|--------|
| `protocol/generated/cpp/protocol.h` | R1 | Add aliases, add gear to HostDriveCmd |
| `rt-esp32/src/config.h` | R2 | Add missing constants |
| `sys-esp32/src/config.h` | R3 | Add missing constants |
| `rt-esp32/src/main.cpp` | R4,R5,R6,R7,R8 | Rewrite — remove UART, add dual CAN, gateway, TX tasks, steering, heartbeat |
| `rt-esp32/src/control_logic.h` | R4 | Change return type to CAN-based structs |
| `rt-esp32/src/control_logic.cpp` | R4 | Build can::RtDriveCmd + can::RtBrakeCmd |
| `rt-esp32/src/can_driver_twai.cpp` | R5 | Real TWAI implementation |
| `rt-esp32/src/can_driver_mcp2515.cpp` | R5 | Real MCP2515 SPI implementation |
| `rt-esp32/src/can_driver_mcp2515.h` | R5 | Keep class interface |
| `rt-esp32/src/can_rx_router.h` | R6 | Remove raw-pointer GatewayQueues |
| `rt-esp32/src/steering_control.cpp` | R8 | Real LBS state machine (from header stub) |
| `rt-esp32/src/heartbeat.cpp` | R8 | Alive counter + validation |
| `sys-esp32/src/main.cpp` | S1,S2,S3,S4 | Rewrite all 15 task stubs with real implementations |
| `sys-esp32/src/brake_actuator.cpp` | S5 | Delete |
| `sys-esp32/src/brake_actuator.h` | S5 | Delete |
| `sys-esp32/src/motor_driver.cpp` | S6 | Move to legacy |
| `rt-esp32/test/test_*.cpp` | R1–R8 | Update tests for new interfaces |
| `legacy/shared/intermcu/` | R4 | No longer referenced by active code |
