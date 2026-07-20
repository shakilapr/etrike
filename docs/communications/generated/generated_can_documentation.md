# Full CAN Network Documentation — E-Trike
**Version:** 1.0
**Description:** E-Trike CAN signal definitions — single source of truth for DBC generation

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 32
- **Total Signal Definitions:** 190
- **Unique Signal Names:** 162
- **Protocols/Buses:** 4
- **ECUs Defined:** 9

---

## CAN Network Topology & Communication Architecture

The E-Trike utilizes two primary CAN networks running at 500 kbit/s:
1. **High-Level CAN Bus:** Connects the Host (Jetson Orin NX) and the RT (Real-Time Gateway). Used for high-level kinematics, drive commands, and telemetry.
2. **Low-Level CAN Bus:** Connects the RT, SYS (Safety/Body controller), MTR (Motor controller), and Actuators (Steer-by-Wire EPS_C and Brake-by-Wire SEB).

### Gateway & Forwarding
The **RT** ECU acts as a physical gateway between the High-Level and Low-Level CAN buses. It selectively bridges and forwards key messages:
- **0x001 (`SAFETY_ESTOP`)**: Bridged bidirectionally (any node can trigger).
- **0x011 (`SYS_SAFETY_STS`)**: Forwarded from Low to High.
- **0x120 (`SYS_THROTTLE_STS`)**: Forwarded from Low to High.
- **0x206 (`MTR_MOTOR_FBK`)**: Forwarded from Low to High.
- **0x302 (`HOST_LIGHT_CMD`)**: Forwarded from High to Low.
- **0x600 (`SYS_DIAG_RPT`)**: Forwarded from Low to High.

### Node Roles & Responsibilities
- **Host:** QM. Transmits Auto-mode drive commands (speed/yaw) and obstacle detection limits.
- **RT:** Gateway and Kinematics. Receives Host commands, calculates kinematics, and issues direct actuator targets (steering/speed) to the Low-Level bus.
- **SYS:** Safety and Body Control. Manages ESTOP states, mode switching, lighting, and overrides brake control during Manual/ESTOP modes.
- **MTR:** Actuation. Drives the physical motor based on CAN inputs and reports feedback.
- **EPS_C & SEB:** Smart steer-by-wire and brake-by-wire actuator modules relying on Intel byte-order sub-protocols.

---

## Type Notation
| Notation | Meaning |
|---|---|
| `signed` / `unsigned` | Signed / Unsigned integer |
| `enum` | Enumeration (value map provided) |
| `bitmask` | Bitfield, each bit is a flag |
| `DLC=0` | Zero-length CAN frame (event signal, no payload) |

## Network Nodes (ECUs)
| Node Name | Description |
|---|---|
| **Host** | Orin NX, Linux + ROS 2, perception/planning |
| **RT** | ESP32-S3, FreeRTOS, realtime kinematics + CAN gateway |
| **SYS** | ESP32-S3, FreeRTOS, safety + motor + body control |
| **MTR** | STM32, bare metal, motor DAC + gear relays (EGAS L1) |
| **DCDC** | DC-DC converter (72V->12V), CAN 0x012 control |
| **EPS_C** | steer-by-wire unit, steer-by-wire module, preprogrammed CAN IDs |
| **SEB** | brake-by-wire unit, electro-hydraulic brake module, preprogrammed CAN IDs |
| **HMI** | CAN Controller UI / Dashboard, mode and power requests |
| **Any** | Wildcard — any node may send (e.g., ESTOP frames) |

---

## Global Constants & Parameters
| Parameter | Value |
|---|---|
| `wheelbase_mm` | `1500` |
| `obstacle_stop_mm` | `300` |
| `obstacle_clear_mm` | `3000` |
| `max_speed_fwd_mmps` | `3000` |
| `max_speed_rev_mmps` | `500` |
| `low_speed_thresh_mmps` | `50` |
| `cmd_stale_timeout_ms` | `500` |
| `heartbeat_timeout_host_ms` | `1500` |
| `heartbeat_timeout_sys_ms` | `200` |
| `heartbeat_timeout_rt_ms` | `1000` |
| `startup_grace_period_ms` | `3000` |
| `brake_stroke_scale` | `0.05` |
| `brake_stroke_offset` | `-30.0` |
| `max_brake_kpa` | `5000` |
| `seb_max_pressure_raw` | `100` |
| `obstacle_max_kpa` | `5000` |
| `assist_stop_kpa` | `2000` |
| `host_brake_max_kpa` | `20000` |
| `steer_hard_limit_deg` | `40.0` |
| `steer_following_err_min_deg` | `2.0` |
| `steer_following_err_factor` | `0.25` |
| `steer_following_err_ms` | `300` |
| `steer_estop_ramp_deg_s` | `20.0` |
| `steer_estop_hold_ms` | `500` |
| `steer_cmd_rate_hz` | `50` |
| `mtr_fault_estop_active` | `1` |
| `mtr_fault_cmd_timeout` | `2` |
| `mtr_fault_adc_fault` | `4` |
| `mtr_fault_gear_conflict` | `8` |

---

## Message Dictionary
### Protocol: `custom_high`
**Physical Bus:** high | **Byte Order:** motorola

#### 0x001 — SAFETY_ESTOP
- **Sender:** Any
- **Receivers:** SYS, Host, MTR, DCDC
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** DLC=0 — the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.

*No payload (DLC=0 event frame)*

#### 0x011 — SYS_SAFETY_STS
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 3 bytes
- **Cycle:** 200 ms (0 = event-based)
- **Description:** Forwarded low→high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_EstopActive` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `SYS_HeartbeatOk` | 1 | 0 | 8 | unsigned | 1 | [0, 1] | - | 0=RT alive counter frozen >1000ms, 1=incrementing |
| `SYS_LightLeft` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_LightRight` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_LightBrake` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_LightHead` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

#### 0x111 — HMI_MODE_REQ
- **Sender:** HMI
- **Receivers:** SYS, Host
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** HMI mode request. 1Hz periodic heartbeat. Forwarded high→low by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HMI_ReqMode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  (Values: 0=MANUAL, 1=AUTO, 2=PURE_SIM) |
| `HMI_ModeAlive` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | Rolling counter for UI health |

#### 0x112 — HMI_PWR_REQ
- **Sender:** HMI
- **Receivers:** SYS
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** HMI power request. 1Hz periodic heartbeat. Forwarded high→low by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HMI_ReqStart` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=False, 1=True) |
| `HMI_PwrAlive` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | Rolling counter for UI health |

#### 0x120 — SYS_THROTTLE_STS
- **Sender:** MTR
- **Receivers:** RT, Host
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** Current vehicle speed from MTR STM32. Forwarded low→high by RT. SYS_ prefix is historical.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_ThrottleSpeed` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | mm/s |  |

#### 0x206 — MTR_MOTOR_FBK
- **Sender:** MTR
- **Receivers:** RT, SYS, Host
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)
- **Description:** Motor feedback from STM32. Forwarded low→high by RT per gateway rules.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `MTR_ActualSpeed` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | mm/s |  |
| `MTR_GearState` | 2 | 0 | 8 | unsigned | 1 | [0, 3] | - |  |
| `MTR_FaultFlags` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - | bit0=ESTOP, bit1=CMD timeout, bit2=ADC fault, bit3=gear conflict, bit4=MTR startup ready |

#### 0x210 — RT_STATE_RPT
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 6 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** RT state report to Host (high bus) and SYS (low bus). SYS monitors safety_state for takeover detection and RT health.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `RT_Mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  (Values: 0=MANUAL, 1=AUTO, 2=ESTOP) |
| `RT_SafetyState` | 1 | 0 | 2 | unsigned | 1 | [0, 2] | - | RT internal state: 0=Normal, 1=Internal ESTOP (steer ramp/hold), 2=Fault (Values: 0=Normal, 1=Warning, 2=Fault) |
| `RT_EstopReason` | 1 | 4 | 4 | unsigned | 1 | [0, 7] | - | Reason for ESTOP state, packed in byte 1 bits 4-7 |
| `RT_Reversing` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `RT_RxOverflow` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - | MCP2515 RX overflow counter — telemetry for CAN bus health monitoring |
| `RT_TaskHealth` | 4 | 0 | 8 | unsigned | 1 | [0, 255] | - | Bitmask of alive tasks (bits 0-3: control/dispatch/tx_low/tx_high) |
| `RT_SteerState` | 5 | 0 | 8 | unsigned | 1 | [0, 5] | - | Steering state machine value |

#### 0x220 — RT_PID_RPT
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 6 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** RESERVED, inactive. PID telemetry for Host debugging.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `RT_PidSetpoint` | 0 | 0 | 16 | signed | 1 | [-32768, 32767] | mm/s |  |
| `RT_PidMeasured` | 2 | 0 | 16 | signed | 1 | [-32768, 32767] | mm/s |  |
| `RT_PidOutput` | 4 | 0 | 16 | signed | 1 | [-32768, 32767] | - |  |

#### 0x300 — HOST_DRIVE_CMD
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** Host (Jetson Orin) Autoware.Auto drive command -> RT. High bus only.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HOST_DriveSpeed` | 0 | 0 | 32 | signed | 1 | [-500, 3000] | mm/s | ROS 2: linear.x * 1000 |
| `HOST_YawRate` | 4 | 0 | 24 | signed | 1 | [-3000, 3000] | mrad/s | ROS 2: angular.z * 1000. i24 big-endian at bytes 4-6. |
| `HOST_Gear` | 7 | 0 | 8 | unsigned | 1 | [0, 3] | enum |  (Values: 0=N, 1=D, 2=S, 3=R) |

#### 0x301 — HOST_BRAKE_REQ
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 4 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** On demand. RT arbitrates: max(RT_computed, HOST_request) -> 0x205.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HOST_BrakePressure` | 0 | 0 | 32 | signed | 1 | [0, 20000] | kPa |  |

#### 0x302 — HOST_LIGHT_CMD
- **Sender:** Host
- **Receivers:** RT, SYS
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** Forwarded transparently high→low by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HOST_LeftTurn` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `HOST_RightTurn` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `HOST_BrakeLight` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `HOST_Headlight` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

#### 0x310 — STEER_DIAG
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** Steering telemetry to Host. v0.0.4 — previously missing from DBC.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SteerDiag_Angle0_1deg` | 0 | 0 | 16 | unsigned | x0.1 + -3000.0 | [0, 65535] | deg | Actual steering angle. physical_deg = raw * 0.1 - 3000. Raw 30000 -> 0deg. |
| `SteerDiag_Fault` | 2 | 0 | 8 | unsigned | 1 | [0, 1] | - | 0=OK, 1=EPS-C fault |
| `SteerDiag_MotorCurrent` | 3 | 0 | 16 | unsigned | x0.01 | [0, 65535] | A | EPS-C motor current, 0.01A/bit |
| `SteerDiag_ECUTemp` | 5 | 0 | 16 | unsigned | x0.1 | [0, 65535] | degC | EPS-C ECU temperature, 0.1degC/bit |
| `SteerDiag_Reserved` | 7 | 0 | 8 | unsigned | 1 | [0, 0] | - |  |

#### 0x311 — BRAKE_DIAG
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** Brake telemetry to Host. v0.0.4 — previously missing from DBC.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `BrakeDiag_PressureRaw` | 0 | 0 | 16 | unsigned | x0.05 | [0, 65535] | MPa | SEB pressure raw, 0.05 MPa/bit |
| `BrakeDiag_Fault` | 2 | 0 | 8 | unsigned | 1 | [0, 1] | - | 0=OK, 1=SEB fault |
| `BrakeDiag_MotorCurrent` | 3 | 0 | 16 | unsigned | x0.01 | [0, 65535] | A | SEB motor current, 0.01A/bit |
| `BrakeDiag_ECUTemp` | 5 | 0 | 16 | unsigned | x0.1 | [0, 65535] | degC | SEB ECU temperature, 0.1degC/bit |
| `BrakeDiag_Reserved` | 7 | 0 | 8 | unsigned | 1 | [0, 0] | - |  |

#### 0x400 — HOST_OBSTACLE_DIST
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 4 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** Host sends min obstacle distance (from LiDAR/camera perception) to RT at 10 Hz. High bus only.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HOST_ObstacleDistance` | 0 | 0 | 32 | unsigned | 1 | [0, 4.29497e+09] | mm | UINT32_MAX = no reading / timeout (Values: 4294967295=clear) |

#### 0x600 — SYS_DIAG_RPT
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** SYS diagnostics report. Forwarded low→high by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_DiagMode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |
| `SYS_DiagBrakeEngaged` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_DiagBrakeFault` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - | SEB L3 fault or brake following-error active |
| `SYS_DiagHeartbeatOk` | 2 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `SYS_DiagEstopActive` | 3 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `SYS_DiagFreeHeapKb` | 4 | 0 | 16 | unsigned | 1 | [0, 65535] | KB |  |
| `SYS_DiagTec` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `SYS_DiagRec` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

#### 0x7FC — HOST_HEARTBEAT
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)
- **Description:** Not bridged, high bus only. Loss triggers controlled stop, not ESTOP.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `Host_AliveCtr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - | Timeout 1500ms -> controlled stop. Host is QM, not safety-critical. |
| `Host_HealthFlags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok, bits4-7=reserved |

#### 0x7FD — RT_HEARTBEAT
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)
- **Description:** RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the high-bus instance.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `RT_AliveCtr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - | High bus timeout 1500ms->Host stops /cmd_vel |
| `RT_HealthFlags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok, bits4-7=reserved |

---

### Protocol: `custom_low`
**Physical Bus:** low | **Byte Order:** motorola

#### 0x001 — SAFETY_ESTOP
- **Sender:** Any
- **Receivers:** SYS, Host, MTR, DCDC
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** DLC=0 — the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.

*No payload (DLC=0 event frame)*

#### 0x011 — SYS_SAFETY_STS
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 3 bytes
- **Cycle:** 200 ms (0 = event-based)
- **Description:** Forwarded low→high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_EstopActive` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `SYS_HeartbeatOk` | 1 | 0 | 8 | unsigned | 1 | [0, 1] | - | 0=RT alive counter frozen >1000ms, 1=incrementing |
| `SYS_LightLeft` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_LightRight` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_LightBrake` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_LightHead` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

#### 0x012 — SYS_DCDC_CMD
- **Sender:** SYS
- **Receivers:** DCDC
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** DC-DC converter control. Low bus only.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_DcdcEnable` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - | ESTOP->1(on); maintains 12V for MCUs, CAN transceivers, brake light |

#### 0x110 — SYS_MODE_CMD
- **Sender:** SYS
- **Receivers:** RT, MTR
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** 0=Manual, 1=Auto, 2=ESTOP. Low bus only. MTR needs mode for pass-through vs CAN control.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_Mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |

#### 0x111 — HMI_MODE_REQ
- **Sender:** HMI
- **Receivers:** SYS, Host
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** HMI mode request. 1Hz periodic heartbeat. Forwarded high→low by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HMI_ReqMode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  (Values: 0=MANUAL, 1=AUTO, 2=PURE_SIM) |
| `HMI_ModeAlive` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | Rolling counter for UI health |

#### 0x112 — HMI_PWR_REQ
- **Sender:** HMI
- **Receivers:** SYS
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** HMI power request. 1Hz periodic heartbeat. Forwarded high→low by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HMI_ReqStart` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=False, 1=True) |
| `HMI_PwrAlive` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | Rolling counter for UI health |

#### 0x120 — SYS_THROTTLE_STS
- **Sender:** MTR
- **Receivers:** RT, Host
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** Current vehicle speed from MTR STM32. Forwarded low→high by RT. SYS_ prefix is historical.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_ThrottleSpeed` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | mm/s |  |

#### 0x204 — RT_DRIVE_CMD
- **Sender:** RT
- **Receivers:** SYS, MTR
- **DLC:** 5 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** MTR receives for motor actuation. SYS receives for EGAS L2 monitoring. ID 0x204 avoids collision with EPS-C 0x202.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `RT_MotorSpeed` | 0 | 0 | 32 | signed | 1 | [-500, 3000] | mm/s |  |
| `RT_Gear` | 4 | 0 | 8 | unsigned | 1 | [0, 3] | enum |  (Values: 0=N, 1=D, 2=S, 3=R) |

#### 0x205 — RT_BRAKE_CMD
- **Sender:** RT
- **Receivers:** SYS
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)
- **Description:** RT max-select: max(rt_obstacle, host_0x301) -> SYS SEB cmd.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `RT_BrakePressure` | 0 | 0 | 32 | signed | 1 | [0, 20000] | kPa |  |

#### 0x206 — MTR_MOTOR_FBK
- **Sender:** MTR
- **Receivers:** RT, SYS, Host
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)
- **Description:** Motor feedback from STM32. Forwarded low→high by RT per gateway rules.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `MTR_ActualSpeed` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | mm/s |  |
| `MTR_GearState` | 2 | 0 | 8 | unsigned | 1 | [0, 3] | - |  |
| `MTR_FaultFlags` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - | bit0=ESTOP, bit1=CMD timeout, bit2=ADC fault, bit3=gear conflict |

#### 0x302 — HOST_LIGHT_CMD
- **Sender:** Host
- **Receivers:** RT, SYS
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)
- **Description:** Forwarded transparently high→low by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `HOST_LeftTurn` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `HOST_RightTurn` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `HOST_BrakeLight` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `HOST_Headlight` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

#### 0x600 — SYS_DIAG_RPT
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** SYS diagnostics report. Forwarded low→high by RT.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_DiagMode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |
| `SYS_DiagBrakeEngaged` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `SYS_DiagBrakeFault` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - | SEB L3 fault or brake following-error active |
| `SYS_DiagHeartbeatOk` | 2 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `SYS_DiagEstopActive` | 3 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `SYS_DiagFreeHeapKb` | 4 | 0 | 16 | unsigned | 1 | [0, 65535] | KB |  |
| `SYS_DiagTec` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `SYS_DiagRec` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

#### 0x7FD — RT_HEARTBEAT
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)
- **Description:** RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the low-bus instance.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `RT_AliveCtr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - | Low bus timeout 1000ms->SYS ESTOP |
| `RT_HealthFlags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | bit0=heartbeat_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok, bits4-7=reserved |

#### 0x7FE — SYS_HEARTBEAT
- **Sender:** SYS
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** Low bus only, never leaves low bus.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SYS_AliveCtr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - | 10 Hz / 200ms timeout -> RT brake takeover + ESTOP |
| `SYS_HealthFlags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - | bit0=heartbeat_ok, bit1=estop_active, bits2-3=reserved |

---

### Protocol: `sbw_unit`
**Physical Bus:** low | **Byte Order:** intel

#### 0x169 — VCU_SES_REQ
- **Sender:** RT
- **Receivers:** EPS_C
- **DLC:** 8 bytes
- **Cycle:** 20 ms (0 = event-based)
- **Description:** steer-by-wire unit command. 50 Hz continuous. Byte 5 overlap: Speed[15:8] shares with security nibble per CSV.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SES_AlignEnable` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - | Angle Initial Alignment Enable. 0=disabled, 1=centering. |
| `SES_CtrlEnable` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - | Direction Control Enable. 0=Disabled, 1=Enable (Angle Control). |
| `SES_TgtStrAngle` | 2 | 0 | 16 | signed | x0.1 + -3000.0 | [-700, 700] | deg | Target Steering Angle. Negative=left. Offset=-3000 per mfr CSV. |
| `SES_TgtStrAngleSpd` | 4 | 0 | 16 | unsigned | 1 | [125, 525] | deg/s | Target Angle Speed. Effective 10-bit: bits 0-7 in byte 4, bits 8-9 in byte 5 bits 2-3. |
| `SES_RollCntEnable` | 5 | 0 | 1 | unsigned | 1 | [0, 1] | - | Life Signal Enable — MUST be 1. |
| `SES_ChecksumEnable` | 5 | 1 | 1 | unsigned | 1 | [0, 1] | - | Checksum Enable — MUST be 1. |
| `SES_RollCnt` | 5 | 4 | 4 | unsigned | 1 | [0, 15] | - | Life Signal rolling counter. Increment every frame. |
| `SES_VehSpd` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | km/h | Vehicle speed populated by RT. |
| `SES_Checksum` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - | Checksum = XOR(bytes 0-6) ^ 0xFF. |

#### 0x201 — SES_STATUS
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** steer-by-wire unit status feedback. 100 Hz. Byte 5 overlap: StrAngleSpd[15:8] / Torq share byte 5 per CSV.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SES_AngleStatus` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - | Center Finding Status. 0=Finding, 1=Found. |
| `SES_CtrlModeStatus` | 0 | 1 | 2 | unsigned | 1 | [0, 3] | - | Control Mode Feedback. 0=Manual, 1=Automatic. |
| `SES_ErrorStatus` | 0 | 6 | 2 | unsigned | 1 | [0, 3] | - | Error Status. |
| `SES_StrAngle` | 2 | 0 | 16 | unsigned | x0.1 + -3000.0 | [-700, 700] | deg | Steering Angle. |
| `SES_TgtStrAngleSpd_FB` | 4 | 0 | 16 | signed | x0.5 | [0, 1480] | deg/s | Angle Speed feedback. |
| `SES_SteeringTorq` | 5 | 0 | 8 | unsigned | x0.1 + -12.1 | [-12, 12] | Nm | Steering Torque. |
| `SES_RollCntEnStatus` | 6 | 0 | 1 | unsigned | 1 | [0, 1] | - | Life Signal Enable Feedback. |
| `SES_ChecksumEnStatus` | 6 | 1 | 1 | unsigned | 1 | [0, 1] | - | Checksum Enable Feedback. |
| `SES_RollCntStatus` | 6 | 4 | 4 | unsigned | 1 | [0, 15] | - | Life Signal Feedback — echoes rolling counter. |
| `SES_ChecksumStatus` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - | Checksum Feedback. |

#### 0x202 — SES_ErrInfo
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** steer-by-wire unit detailed fault flags. 8 L3 faults (redundant sensor loss) -> RT must escalate to ESTOP.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SES_ECUUnderVolt` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - | Controller Under Voltage [L2] |
| `SES_ECUOverVolt` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - | Controller Over Voltage [L2] |
| `SES_CanComErr` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - | CAN Communication Fault [L1] |
| `SES_ECUTempErr` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - | Controller Temp Fault [L1] |
| `SES_DomainSC` | 0 | 4 | 1 | unsigned | 1 | [0, 1] | - | Domain Drive Short Circuit [L2] |
| `SES_DomainV` | 0 | 5 | 1 | unsigned | 1 | [0, 1] | - | Domain Drive Voltage Fault [L2] |
| `SES_DomainT` | 0 | 6 | 1 | unsigned | 1 | [0, 1] | - | Domain Drive Temperature Fault [L2] |
| `SES_TempSensor` | 0 | 7 | 1 | unsigned | 1 | [0, 1] | - | Temperature Sensor Fault |
| `SES_AngleP_OC` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor Pri. Open Circuit [L3] |
| `SES_AngleP_AF` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor Pri. Out of Range [L3] |
| `SES_AngleS_OC` | 1 | 2 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor Sec. Open Circuit [L3] |
| `SES_AngleS_AF` | 1 | 3 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor Sec. Out of Range [L3] |
| `SES_SensorPow` | 1 | 4 | 1 | unsigned | 1 | [0, 1] | - | Sensor Power Fault [L2] |
| `SES_Alignment` | 1 | 5 | 1 | unsigned | 1 | [0, 1] | - | Centering Fault [L1] |
| `SES_OverAngle` | 1 | 6 | 1 | unsigned | 1 | [0, 1] | - | Over Angle Fault [L2] |
| `SES_StrMtrStall` | 1 | 7 | 1 | unsigned | 1 | [0, 1] | - | Motor Stall Fault [L1] |
| `SES_MtrCurtFault` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - | Motor Current Fault [L2] |
| `SES_SensorCL` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - | Sensor 5V Power Fault [L2] |
| `SES_TorqT1_OC` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - | Torque Sensor T1 Open Circuit [L3] |
| `SES_TorqT1_AF` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - | Torque Sensor T1 Out of Range [L3] |
| `SES_TorqT2_OC` | 2 | 4 | 1 | unsigned | 1 | [0, 1] | - | Torque Sensor T2 Open Circuit [L3] |
| `SES_TorqT2_AF` | 2 | 5 | 1 | unsigned | 1 | [0, 1] | - | Torque Sensor T2 Out of Range [L3] |
| `SES_SentAngle` | 2 | 6 | 1 | unsigned | 1 | [0, 1] | - | Angle Error [L1] |
| `SES_StrMtrIdling` | 2 | 7 | 1 | unsigned | 1 | [0, 1] | - | Motor Idling Fault [L2] |
| `SES_EPROM` | 3 | 0 | 1 | unsigned | 1 | [0, 1] | - | EEPROM Fault [L2] |
| `SES_VehSpdSnapshot` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | km/h | Vehicle speed at fault snapshot. |

#### 0x203 — SES_Version
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** steer-by-wire unit firmware version. Log on boot for compatibility check.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SES_SW_Version` | 0 | 0 | 8 | unsigned | x0.01 | [0, 2.55] | - | Software version (e.g. 0x64 = 1.00) |
| `SES_HW_Version` | 1 | 0 | 8 | unsigned | x0.1 | [0, 25.5] | - | Hardware version (e.g. 0x0D = 1.3) |

#### 0x6FA — SES_Test
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** steer-by-wire unit telemetry. 100 Hz. Bytes 0,7 reserved. Narrower ranges than brake SEB_Test.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SES_MtrCurt` | 1 | 0 | 16 | signed | x0.0078125 | [0, 60] | A | Motor current. Monitor for mechanical binding / rack damage. |
| `SES_ECUTemp` | 3 | 0 | 16 | unsigned | x0.5 | [0, 255] | degC | ECU temperature. For thermal throttling. |
| `SES_PowVolt` | 5 | 0 | 16 | unsigned | x0.00390625 | [0, 18] | V | Supply voltage. 0-18V range. |

---

### Protocol: `bbw_unit`
**Physical Bus:** low | **Byte Order:** intel

#### 0x7B9 — VCU_SEB_REQ
- **Sender:** SYS
- **Receivers:** SEB
- **DLC:** 8 bytes
- **Cycle:** 20 ms (0 = event-based)
- **Description:** brake-by-wire unit brake command. 50 Hz continuous. Byte 3 mode-mux: Stroke[15:8] in Mode 0, Pressure in Mode 1.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SEB_AlignEnable` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - | Calibration enable. |
| `SEB_CtrlEnable` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - | Active control enable. |
| `SEB_CtrlMode` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - | 0=Stroke (position), 1=Pressure (hydraulic). |
| `SEB_AutoBrake` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - | Auto-brake / emergency trigger. |
| `SEB_StrokeReq` | 2 | 0 | 16 | unsigned | 1 | [0, 65535] | raw | Stroke position raw counts. |
| `SEB_PressureReq` | 3 | 0 | 8 | unsigned | 1 | [0, 100] | raw | Pressure raw counts. |
| `SEB_RollCntEnable` | 6 | 0 | 1 | unsigned | 1 | [0, 1] | - | Life Signal Validity — MUST be 1. |
| `SEB_ChecksumEnable` | 6 | 1 | 1 | unsigned | 1 | [0, 1] | - | Checksum Validity — MUST be 1. |
| `SEB_RollCnt` | 6 | 4 | 4 | unsigned | 1 | [0, 15] | - | Life Signal rolling counter. |
| `SEB_Checksum` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - | Checksum = XOR(bytes 0-6) ^ 0xFF. |

#### 0x721 — SEB_STATUS
- **Sender:** SEB
- **Receivers:** SYS, RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** brake-by-wire unit status feedback. 100 Hz. RT monitors pressure for 0x311 BRAKE_DIAG. SYS usage: boot sync -> read StrokeValue; active -> confirm AlignStatus==1.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SEB_AlignStatus` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - | Alignment Info Feedback. 1=aligned. |
| `SEB_CtrlEnStatus` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - | Control Enable Feedback. |
| `SEB_CtrlModeStatus` | 0 | 2 | 2 | unsigned | 1 | [0, 3] | - | Control Mode Feedback. |
| `SEB_AutoBrakeStatus` | 0 | 4 | 1 | unsigned | 1 | [0, 1] | - | Auto Brake Status Feedback. |
| `SEB_ErrorStatus` | 0 | 6 | 2 | unsigned | 1 | [0, 3] | - | Error Status. |
| `SEB_StrokeValue` | 2 | 0 | 16 | unsigned | 1 | [0, 65535] | raw | Stroke Value raw counts. |
| `SEB_PressureValue` | 3 | 0 | 8 | unsigned | 1 | [0, 100] | raw | Pressure raw counts. |
| `SEB_AngleValue` | 5 | 0 | 16 | signed | 1 | [-32768, 32767] | - | Angle raw counts. |
| `SEB_RollCntEnStatus` | 6 | 0 | 1 | unsigned | 1 | [0, 1] | - | Life Signal Status Feedback. |
| `SEB_ChecksumEnStatus` | 6 | 1 | 1 | unsigned | 1 | [0, 1] | - | Checksum Status Feedback. |
| `SEB_RollCntStatus` | 6 | 4 | 4 | unsigned | 1 | [0, 15] | - | Life Signal Feedback — echoes rolling counter. |
| `SEB_ChecksumStatus` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - | Checksum Feedback. |

#### 0x731 — SEB_ErrInfo
- **Sender:** SEB
- **Receivers:** SYS
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)
- **Description:** brake-by-wire unit detailed fault flags. 16 of 23 faults are L3 -> SYS must escalate to ESTOP.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SEB_ECUUnderVolt` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - | Controller Undervoltage [L2] |
| `SEB_ECUOverVolt` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - | Controller Overvoltage [L2] |
| `SEB_CanComErr` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - | CAN Communication Fault [L3] |
| `SEB_ECUTempErr` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - | Controller Temperature Fault [L3] |
| `SEB_DomainSC` | 0 | 4 | 1 | unsigned | 1 | [0, 1] | - | Domain Drive Short Circuit [L3] |
| `SEB_DomainV` | 0 | 5 | 1 | unsigned | 1 | [0, 1] | - | Domain Drive Voltage Fault [L3] |
| `SEB_DomainT` | 0 | 6 | 1 | unsigned | 1 | [0, 1] | - | Domain Drive Temperature Fault [L3] |
| `SEB_AngleP_OC` | 0 | 7 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor P Open Circuit [L3] |
| `SEB_AngleP_AF` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor P Mainboard Abnormal [L3] |
| `SEB_AngleS_OC` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor S Open Circuit [L3] |
| `SEB_AngleS_AF` | 1 | 2 | 1 | unsigned | 1 | [0, 1] | - | Angle Sensor S Sub-board Abnormal [L3] |
| `SEB_NoPreSensor` | 1 | 3 | 1 | unsigned | 1 | [0, 1] | - | Unconnected Oil Pressure Sensor [L3] |
| `SEB_SensorUCL` | 1 | 5 | 1 | unsigned | 1 | [0, 1] | - | Sensor Plausibility Fault [L3] |
| `SEB_AlignmentErr` | 1 | 6 | 1 | unsigned | 1 | [0, 1] | - | Alignment Fault [L2] |
| `SEB_AngleOver` | 1 | 7 | 1 | unsigned | 1 | [0, 1] | - | Angle Out of Bounds [L2] |
| `SEB_MtrStall` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - | Motor Stall Fault [L3] |
| `SEB_MtrDC` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - | Motor Disconnect Fault [L3] |
| `SEB_OilErr` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - | Oil Pressure Error [L2] |
| `SEB_InitOil` | 2 | 4 | 1 | unsigned | 1 | [0, 1] | - | Initial Oil Pressure Fault [L3] |
| `SEB_SentValue` | 2 | 5 | 1 | unsigned | 1 | [0, 1] | - | Send Value Error [L3] |
| `SEB_MtrNoLoad` | 2 | 6 | 1 | unsigned | 1 | [0, 1] | - | Motor No-load Fault [L3] |
| `SEB_PreSensorOver` | 3 | 0 | 1 | unsigned | 1 | [0, 1] | - | Oil Pressure Sensor Overvoltage [L2] |
| `SEB_LowVoltCharging` | 3 | 1 | 1 | unsigned | 1 | [0, 1] | - | Low Voltage Charging Failure [L2] |

#### 0x741 — SEB_Version
- **Sender:** SEB
- **Receivers:** SYS
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** brake-by-wire unit firmware version. Log on boot for compatibility check.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SEB_SW_Version` | 0 | 0 | 8 | unsigned | x0.01 | [0, 2.55] | - | Software version (e.g. 0xC8 = 2.00) |
| `SEB_HW_Version` | 1 | 0 | 8 | unsigned | x0.1 | [0, 25.5] | - | Hardware version (e.g. 0x0D = 1.3) |

#### 0x6FB — SEB_Test
- **Sender:** SEB
- **Receivers:** SYS, RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)
- **Description:** brake-by-wire unit telemetry. 100 Hz. RT monitors for 0x311 BRAKE_DIAG population.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `SEB_MtrCurr` | 1 | 0 | 16 | signed | x0.0078125 | [-255, 255] | A | Motor current. Monitor for mechanical binding. |
| `SEB_ECUTemp` | 3 | 0 | 16 | unsigned | x0.5 + -40.0 | [-40, 215] | degC | ECU temperature. Offset=-40 per manufacturer CSV physical range (raw 0 = -40 degC). |
| `SEB_PowVolt` | 5 | 0 | 16 | unsigned | x0.00390625 | [0, 32] | V | Supply voltage. 0-32V range. |

---
