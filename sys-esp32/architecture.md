# SYS ESP32-S3 Firmware Architecture

> **Firmware Version:** `v0.8.0-alpha-vehicle` / `v0.8.0-alpha-bench`  
> **Target Hardware:** ESP32-S3-WROOM-1 / DevKitC-1-N16R8 (16 MB Flash, 8 MB Octal PSRAM)  
> **Framework:** ESP-IDF 5.x via PlatformIO | FreeRTOS (1000 Hz tick)  
> **CAN Controller:** On-chip TWAI @ 500 kbit/s (GPIO5 TX, GPIO4 RX) connected to Low CAN Bus

---

## 1. System Role & Authority Boundaries

`sys-esp32` is the **Master Safety Authority, Mode Authority, and Body Controller** of the vehicle:

```
                  ┌──────────────────────────────────────────────┐
                  │                 sys-esp32                    │
                  │  Safety Authority & Master Mode Controller   │
                  └───────┬──────────────┬──────────────┬────────┘
                          │              │              │
       0x110 SYS_MODE_CMD │              │              │ 0x7B9 VCU_SEB_REQ
       0x113 SYS_PWR_CMD  │              │              │ (Sole Normal Producer)
                          ▼              │              ▼
                  ┌──────────────┐       │       ┌──────────────┐
                  │  mtr-stm32   │       │       │  SEB Brake   │
                  │ Traction/72V │       │       │ Smart Actuat.│
                  └──────────────┘       │       └──────────────┘
                                         ▼
                                 ┌──────────────┐
                                 │   rt-esp32   │
                                 │ Kinematics/  │
                                 │ Gateway Host │
                                 └──────────────┘
```

### Core Invariants & Boundaries
1. **Master Mode Authority:** SYS owns the vehicle mode state machine (`MANUAL` ↔ `AUTO`, overlaid with latched `ESTOP`). All other nodes follow `0x110 SYS_MODE_CMD`.
2. **High-Voltage Power Interlock:** SYS broadcasts `0x113 SYS_PWR_CMD` (10 Hz). `mtr-stm32` interlocks its 72V contactor relays to this stream.
3. **Sole Normal Brake Producer:** SYS is the sole regular writer of `0x7B9 VCU_SEB_REQ` (50 Hz). `rt-esp32` only produces `0x7B9` if SYS experiences a catastrophic bus-loss (`EMERGENCY_FALLBACK`).
4. **No Direct Motor Actuation:** Direct throttle DAC, gear relays, and ADC readings are retired on SYS; motor propulsion is executed by `mtr-stm32` based on RT `0x204` drive commands.
5. **Host Speed & Runaway Authority:** Speed, wheel odometry, and runaway tracking are supervised by the Host autonomy computer (using LiDAR, IMU, cameras, and odometry). Artificial command-path comparisons (0x204 vs 0x206 echo) are retired on SYS to prevent nuisance ESTOP lockouts.

---

## 2. Hardware Interfaces & Pinout

All GPIOs are initialized in `init_board_gpio()` before tasks start:

| Pin Name | GPIO | Direction | Type / Default | Function |
|---|---:|---|---|---|
| `kEstopGpio` | 1 | Input | Internal Pull-up (NC to GND) | Red mushroom ESTOP button (0V/GND = Safe, HIGH/Open = Active ESTOP) |
| `kBrakeLeverGpio` | 2 | Input | Internal Pull-up | Physical handlebar brake lever (Active LOW) |
| `kStartBtnGpio` | 41 | Input | Internal Pull-up (NC to GND) | Green **latching** NC run/enable button (0V/GND = idle/released, HIGH/Open = latched; latched = run enabled, released = motion inhibited) |
| `kModeBtnGpio` | 11 | Input | Internal Pull-up | Mode toggle button (MANUAL ↔ AUTO; 3s hold exits ESTOP) |
| `DEVELOPER_OVERRIDE_PIN` | 42 | Input | Internal Pull-up | Jumper to GND enables developer bench bypass mode |
| `kSwitchLeftTurn` | 9 | Input | Internal Pull-up | Left turn signal toggle switch (MANUAL mode) |
| `kSwitchRightTurn` | 6 | Input | Internal Pull-up | Right turn signal toggle switch (MANUAL mode) |
| `kSwitchHeadlight` | 7 | Input | Internal Pull-up | Headlight toggle switch (MANUAL mode) |
| `kCanTxGpio` | 5 | Output | TWAI TX | 500 kbit/s Low CAN bus |
| `kCanRxGpio` | 4 | Input | TWAI RX | 500 kbit/s Low CAN bus |
| `kLightBrake` | 21 | Output | Push-Pull | 12V brake light relay driver (relay active-LOW → GPIO LOW = lamp ON; no ULN2803A) |
| `kBulbAuto` | 10 | Output | Push-Pull | Dash indicator: AUTO mode active |
| `kBulbManual` | 39 | Output | Push-Pull | Dash indicator: MANUAL mode active |
| `kBulbReady` | 17 | Output | Push-Pull | Green system-READY LED — ON whenever the whole Host→RT→SYS→{MTR,SEB} path is up and error-free (any non-Blocked `SystemReadyLevel`), OFF on a real fault. See §System READY. |
| `kBulbEstop` | 18 | Output | Push-Pull | Red ESTOP indicator LED |
| `kBulbBypass` | 14 | Output | Push-Pull | Amber developer bypass indicator LED |
| `kPower12vRelay` | 40 | Output | Push-Pull | 12V auxiliary power relay (energized in all modes, including ESTOP, so indicator lamps stay powered) |

---

## 3. Concurrency Architecture (12 Tasks)

The firmware runs 12 preemptive FreeRTOS tasks configured in `app_main()`:

```
Priority 5: [task_can_rx]        [task_safety (20 Hz)]
                   │
                   ▼ (g_can_rx_queue)
Priority 4: [task_dispatch]      [task_mode (10 Hz)]
Priority 3: [task_brake (50 Hz)] [task_lights (20 Hz)] [task_gear (50 Hz)]
Priority 2: [task_can_tx (5 Hz)] [task_can_ctrl (50 Hz)][task_indicator (25 Hz)]
Priority 1: [task_hb (10 Hz)]    [task_diag (1 Hz)]
```

### Detailed Task Specifications

| Task Name | Priority | Stack | Cadence | Functions & Execution Flow |
|---|---:|---:|---:|---|
| `task_can_rx` | 5 | 4608 B | Event | Blocks on `g_can.receive()` from TWAI driver; yields 5 ms on congestion and forwards frame to `g_can_rx_queue`. |
| `task_safety` | 5 | 4608 B | 20 Hz | Polls ESTOP button and brake lever GPIOs; checks RT heartbeat timeout (`0x7FD`); enforces `0x204` setpoint staleness; checks MTR feedback (`0x206`) staleness. |
| `task_dispatch` | 4 | 3584 B | Event | Dequeues from `g_can_rx_queue`; parses incoming CAN frames (`0x204`, `0x205`, `0x111`, `0x112`, `0x114`, `0x206`, `0x302`, `0x001`, `0x721`, `0x6FB`, `0x731`, `0x741`, `0x210`, `0x122`, `0x7FD`); unpacks payload into shared atomic state. |
| `task_mode` | 4 | 2560 B | 10 Hz | Debounces START/MODE buttons; START spans the run/enable latch; MODE 5 s runs the validated reset; evaluates optional wheel EGAS; calculates `resolve_authority()`; broadcasts `0x110 SYS_MODE_CMD` and `0x113 SYS_PWR_CMD`. |
| `task_brake` | 3 | 3584 B | 50 Hz | Runs `BrakeControl` state machine; arbitrates priority (ESTOP > Lever > Pressure); encodes and transmits `0x7B9 VCU_SEB_REQ`; monitors `0x721` staleness. |
| `task_lights` | 3 | 2560 B | 20 Hz | Flashes turn signals (500 ms period); handles handlebar switches vs `0x302` commands; sets `kLightBrake` relay; updates `g_light_state`. |
| `task_gear` | 3 | 2048 B | 50 Hz | Compares `0x204` commanded gear vs `0x206` reported gear state; logs error on persistent mismatch. |
| `task_indicator` | 2 | 2560 B | 25 Hz | Drives `kBulbAuto`, `kBulbManual`, `kBulbReady`, `kBulbEstop`, `kBulbBypass`, and `kPower12vRelay` GPIOs (via the polarity-aware `set_relay()` helper). |
| `task_can_tx` | 2 | 3584 B | 5 Hz | Encodes and sends `0x011 SYS_SAFETY_STS` (with CRC-8) and `0x500 SYS_NODE_STATUS`. |
| `task_can_control`| 2 | 2560 B | 50 Hz | Calls `g_can.service_recovery()` every 20 ms to monitor bus-off and execute exponential backoff recovery. |
| `task_hb` | 1 | 2560 B | 10 Hz | Encodes and transmits `0x7FE SYS_HEARTBEAT` with rolling counter and 4 task health bits. |
| `task_diag` | 1 | 3584 B | 1 Hz | Evaluates task alive counters against 1.5s deadline; forces ESTOP if critical tasks stall $\ge 2\text{s}$; transmits `0x600 SYS_DIAG_RPT`. |

---

## 4. Safety Architecture & Fault Management

### 4.1 Two-Mask Fault Architecture (`inhibit_state.h`)

SYS separates transient/recoverable degradation from permanent safety-latched faults:

1. **Transient Inhibits (`InhibitReason`):**
   - `kInhibitMtrFbkLoss`: `0x206` missing $> 200\,\text{ms}$.
   - `kInhibitSebCommsLoss`: `0x721` missing $> 100\,\text{ms}$ (or not seen within 1000 ms of boot).
   - **Action:** Clamps `0x110` mode to MANUAL and drops `0x113` power to OFF. Motion is inhibited.
   - **Recovery:** Clears automatically after 3 consecutive healthy frames.

2. **Latched Safety Faults (`LatchedFaultReason`):**
   - `kLatchedSebL3`: `0x721` reports error status $\ge 3$ or `0x731` reports Level 3 error bits (actuator internal hardware fault / overcurrent / motor stall).
   - **Action:** Clamps `0x110` mode to MANUAL and drops `0x113` power to OFF, cutting positive propulsion while keeping steer-by-wire (SES) operational and 12V auxiliary power intact (does not force ESTOP or broadcast `0x001`).
   - **Recovery:** Requires an explicit validated reset transaction (MODE 5 s long-press) while `latched_causes_currently_clearable()` returns true.

### 4.2 Authenticated Remote ESTOP Reset (BUG-10)
Host can clear an ESTOP over CAN via `0x114 HOST_ESTOP_RESET_REQ`:
- Validates token `0x5253` ('RS') and rolling counter freshness.
- Evaluates blockers via `get_estop_reset_blockers()`:
  - Rejects if hardware button active (`kResetBlockPhysicalEstop`).
  - Rejects if latched fault still asserted (`kResetBlockLatchedFault`).
  - Rejects if vehicle speed $> 50\,\text{mm/s}$ (`kResetBlockMoving`).
  - Rejects if RT heartbeat lost (`kResetBlockHeartbeatLoss`).
  - Rejects if MTR never acknowledged ESTOP (`kResetBlockMtrEstopActive`, checks `!mtr_ack_confirmed` to prevent staged reset deadlock).
  - Rejects if transient inhibit active (`kResetBlockTransientInhibit`).
- Emits `0x115 SYS_ESTOP_RESET_RSP` with ACCEPTED/REJECTED and active blocker mask.

---

## 5. CAN Communication Matrix

```
Low CAN Bus (500 kbit/s, 11-bit Standard ID)
┌───────────┬──────────────────────┬─────────┬──────────┬──────────────────────────────────────────┐
│ CAN ID    │ Name                 │ Dir     │ Period   │ Payload & Function                       │
├───────────┼──────────────────────┼─────────┼──────────┼──────────────────────────────────────────┤
│ 0x001     │ SAFETY_ESTOP         │ RX/TX   │ Event    │ DLC 0. Hard emergency stop broadcast     │
│ 0x011     │ SYS_SAFETY_STS       │ TX      │ 200 ms   │ DLC 5. estop_source, node_presence, reason│
│ 0x110     │ SYS_MODE_CMD         │ TX      │ 100 ms   │ DLC 2. Mode authority (0=MANUAL, 1=AUTO) │
│ 0x111     │ HMI_MODE_REQ         │ RX      │ 1000 ms  │ DLC 2. Host requested mode (StreamValid) │
│ 0x112     │ HMI_PWR_REQ          │ RX      │ 1000 ms  │ DLC 2. Host requested power state        │
│ 0x113     │ SYS_PWR_CMD          │ TX      │ 100 ms   │ DLC 2. Contactor power authority (0/1)   │
│ 0x114     │ HOST_ESTOP_RESET_REQ │ RX      │ Event    │ DLC 4. Remote reset request with token   │
│ 0x115     │ SYS_ESTOP_RESET_RSP  │ TX      │ Event    │ DLC 5. Reset reply + blocker bitmask     │
│ 0x122     │ RT_WHEEL_SPEED_STS   │ RX      │ 100 ms   │ DLC 4. Measured speed (optional EGAS)    │
│ 0x204     │ RT_DRIVE_CMD         │ RX      │ 10 ms    │ DLC 5. Speed setpoint mm/s + gear cmd    │
│ 0x205     │ RT_BRAKE_CMD         │ RX      │ 20 ms    │ DLC 4. Automated brake request (kPa)     │
│ 0x206     │ MTR_MOTOR_FBK        │ RX      │ 20 ms    │ DLC 4. Speed cmd echo + fault flags      │
│ 0x210     │ RT_STATE_RPT         │ RX      │ 100 ms   │ DLC 6. RT safety state for takeover      │
│ 0x302     │ HOST_LIGHT_CMD       │ RX      │ Event    │ DLC 1. Turn, brake, headlight bits       │
│ 0x500     │ SYS_NODE_STATUS      │ TX      │ 200 ms   │ DLC 6. Exec state, blockers, hw inputs,  │
│           │                      │         │          │ relay/lamp outputs                       │
│ 0x501     │ RT_NODE_STATUS       │ RX      │ 20 ms    │ DLC 8. RT ready/degraded (covers SES)    │
│ 0x502     │ MTR_NODE_STATUS      │ RX      │ 20 ms    │ DLC 8. MTR ready/output/ignition proof   │
│ 0x600     │ SYS_DIAG_RPT         │ TX      │ 1000 ms  │ DLC 8. Bus health, TEC/REC, task health, │
│           │                      │         │          │ heap, reset reason, uptime               │
│ 0x6FB     │ SEB_TEST             │ RX      │ 10 ms    │ DLC 8. Motor current & ECU temperature   │
│ 0x721     │ SEB_STATUS           │ RX      │ 10 ms    │ DLC 8. Stroke raw, error status, roll    │
│ 0x731     │ SEB_ERR_INFO         │ RX      │ 100 ms   │ DLC 8. 16 Level-3 vendor fault bits      │
│ 0x741     │ SEB_VERSION          │ RX      │ 1000 ms  │ DLC 8. SEB hardware/software version     │
│ 0x7B9     │ VCU_SEB_REQ          │ TX      │ 20 ms    │ DLC 8. Commanded stroke/pressure to SEB  │
│ 0x7FD     │ RT_HEARTBEAT         │ RX      │ 500 ms   │ DLC 2. RT alive counter + health flags   │
│ 0x7FE     │ SYS_HEARTBEAT        │ TX      │ 100 ms   │ DLC 2. SYS alive counter + 4 task flags  │
└───────────┴──────────────────────┴─────────┴──────────┴──────────────────────────────────────────┘
```

---

## 6. System READY (observational)

`SystemReadyLevel` (see `src/system_ready.h`) reports whether the whole
Host → RT → SYS → {MTR, SEB} command path is up and error-free — i.e. "if the
Host commands ignition/gear/speed, it will actually go". It is **purely
observational**: it never feeds `resolve_authority()`, `inhibit_state`, ESTOP, or
any outgoing frame, so a display signal can never change actuation authority.

| Level | Condition | Green `kBulbReady` | WS2812 |
|-------|-----------|--------------------|--------|
| `Blocked` | SYS ESTOP/inhibit, or RT absent, or a required peer (MTR/SEB) absent | OFF | existing fault cascade (Red/Cyan/Yellow) |
| `MtrAbsent` | system up, only MTR missing (bench bypass waives it) | ON | Green **FastBlink** |
| `HostAbsent` | system up, no valid Host `0x111/0x112` request stream | ON | Green **Breathe** |
| `Full` | all present, healthy, Host commanding | ON | normal mode colour (AUTO=Green) |

Evidence: SYS-local latch/inhibit, RT `0x7FD` + `0x501` (RT's `ready`/`degraded`
folds in SES/steering health), MTR `0x502`, SEB `0x721` (fresh ← `kNodeStatusFreshMs`,
rolling advancing, `error_status < 3`), and the Host request-stream validity flags.
Bypass-aware: `g_bypass_mtr_absent` / `g_bypass_seb_sync` set `mtr_required` /
`seb_required` false so an intentionally-absent bench actuator degrades the level
instead of reporting a hard fault. RT is always required. The level is debounced
by `kSystemReadyHoldMs` before it changes, and is exported as `0x500.system_ready`
(Full only) plus `0x500.command_received` / `.command_nonzero`.

---

## 7. Multi-Task Watchdog & Alive Counters

`task_diag` monitors 8 per-task atomic counters updated in each task's loop:
- `g_alive_safety`
- `g_alive_brake`
- `g_alive_dispatch`
- `g_alive_can_tx`
- `g_alive_can_ctrl`
- `g_alive_hb`
- `g_alive_mode`
- `g_alive_gear`

If any task among the critical mask (`safety | brake | dispatch | can_tx | mode`) fails to refresh within $1500\,\text{ms}$ for two consecutive 1-second cycles, SYS assumes internal scheduling failure, forces `ESTOP`, and broadcasts `0x001`.
