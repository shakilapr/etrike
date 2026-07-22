# Controller I/O and CAN Messaging Breakdown

This document provides a highly detailed, deeply structured breakdown of the hardware Inputs/Outputs, CAN messaging payloads, and internal data structures for the **RT (Real-Time)** and **SYS (Safety)** ESP32-S3 controllers.

---

## 1. RT Controller (Real-Time Physics, Steering & Gateway)

### 1.1 Hardware Inputs & Outputs (GPIO)

| GPIO Pin | Direction | Function | Description |
|:---|:---|:---|:---|
| **4** | Input | TWAI RX | Low-Level CAN Bus receive |
| **5** | Output | TWAI TX | Low-Level CAN Bus transmit |
| **15** | Output | SPI SCK | Clock for MCP2515 (High-Level CAN) |
| **16** | Output | SPI MOSI | Data Out for MCP2515 (High-Level CAN) |
| **17** | Input | SPI MISO | Data In for MCP2515 (High-Level CAN) |
| **18** | Output | SPI CS | Chip Select for MCP2515 (High-Level CAN) |
| **47** | Input | MCP INT | Interrupt from MCP2515 (active-low, falling edge) |
| **21** | Output | WDT Toggle | Toggles an external TPS3850 Hardware Watchdog |
| **42** | Input | OVERRIDE | Active-low Mode 1 developer override; jumper to GND (external JTAG MTMS unavailable while connected) |
| **1 / 2** | Input | PCNT (Enc) | Quadrature encoder A/B for rear motor speed feedback |
| **10 / 6** | Input | PCNT (Enc) | Quadrature encoder A/B for front wheel speed |
| **9 / 12** | Input | PCNT (Enc) | Quadrature encoder A/B for rear-left wheel |
| **13 / 14** | Input | PCNT (Enc) | Quadrature encoder A/B for rear-right wheel |

### 1.2 Internal Data Structures & Synchronization

RT strictly adheres to a lock-free design using `std::atomic` for sensor values and FreeRTOS queues for events.

#### Global Atomics
| Atomic Variable | Type | Purpose |
|:---|:---|:---|
| `g_mode_current` | `atomic<uint8_t>` | Stores current vehicle mode (0=Manual, 1=Auto). |
| `g_brake_request_kpa` | `atomic<int32_t>` | Arbitrated brake pressure request. |
| `g_obstacle_mm` | `atomic<uint32_t>` | Distance to nearest obstacle from Jetson (mm). |
| `g_ses_angle_0_1deg` | `atomic<int32_t>` | Actual steering angle feedback (in 0.1°). |
| `g_mtr_actual_speed_mmps`| `atomic<int32_t>` | Actual motor speed feedback (mm/s). |
| `g_estop_reason` | `atomic<uint8_t>` | Tracks cause of ESTOP (0=None, 1=BusOff, 2=FollowingErr, etc). |
| `g_seb_takeover` | `atomic<bool>` | Flag indicating SYS took over brakes due to a timeout. |
| `g_last_sys_hb_us` | `atomic<int64_t>` | Microsecond timestamp of last valid SYS heartbeat. |
| `g_last_host_hb_us` | `atomic<int64_t>` | Microsecond timestamp of last valid Host heartbeat. |

#### Global Queues
| Queue Name | Depth | Payload Type | Description |
|:---|:---|:---|:---|
| `g_safety_evt_q` | 16 | `SafetyEvent` | Safety events triggering mode changes. |
| `g_can_rx_low_q` | 16 | `can::Frame` | Buffers raw frames from TWAI. |
| `g_can_rx_high_q`| 16 | `can::Frame` | Buffers raw frames from MCP2515. |
| `g_gw_tx_low_q` | 8 | `can::Frame` | Gateway queue for forwarding frames Low → High. |
| `g_cmd_q` | 1 | `HostDriveCmd` | Overwrite queue for the latest speed/yaw target from Jetson. |

### 1.3 CAN Messages - Received (RX)

#### `0x300` HOST_DRIVE_CMD (High Bus, 8 bytes, ≤100Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `HOST_DriveSpeed` | 0 | 32 | i32 | Target motor speed (mm/s) |
| `HOST_YawRate` | 32 | 24 | i24 | Target yaw rate (mrad/s) |
| `HOST_Gear` | 56 | 8 | u8 | Gear state (0=N, 1=D, 2=S, 3=R) |

#### `0x301` HOST_BRAKE_REQ (High Bus, 4 bytes, Demand)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `HOST_BrakePressure` | 0 | 32 | i32 | Target brake pressure (kPa) |

#### `0x400` HOST_OBSTACLE_DIST (High Bus, 4 bytes, 10Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `HOST_ObstacleDist` | 0 | 32 | u32 | Min obstacle distance from perception (mm) |

#### `0x7FC` HOST_HEARTBEAT (High Bus, 2 bytes, 2Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame |
| `health_flags` | 8 | 8 | u8 | bit0=hb_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok |

#### `0x201` SES_STATUS (Low Bus, 8 bytes, 100Hz, Little Endian)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `SES_INF_Angle_Status` | 0 | 1 | bool | Center finding status |
| `SES_Control_Mode` | 1 | 2 | u8 | 0=Manual, 1=Auto |
| `SES_Error_Status` | 6 | 2 | u8 | 0=Normal, 1=L1, 2=L2, 3=L3 |
| `SES_StrAngle` | 16 | 16 | u16 | Raw steering angle (offset applied) |
| `SES_Tgt_StrAngleSpd` | 32 | 16 | i16 | Target angle speed |
| `EPS_SteerWheel_Torq` | 40 | 8 | u8 | Steering wheel torque feedback |
| `SES_RollCnt_Status` | 52 | 4 | u8 | Rolling counter echo |
| `SES_CheckSum_Status`| 56 | 8 | u8 | XOR Checksum |

#### `0x721` SEB_STATUS (Low Bus, 8 bytes, 100Hz, Little Endian)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `SEB_Alignment_Status` | 0 | 1 | bool | Alignment info feedback |
| `SEB_Control_Mode` | 2 | 2 | u8 | 0=Stroke, 1=Pressure |
| `SEB_Error_Status` | 6 | 2 | u8 | 0=No fault, 1=L1, 2=L2, 3=L3 |
| `SEB_Stroke_Value` | 16 | 16 | u16 | Stroke position feedback (mm) |
| `SEB_Pressure_Value` | 24 | 8 | u8 | Pressure feedback (mode muxed with stroke) |
| `SEB_Angle_Value` | 40 | 16 | i16 | Angle feedback |
| `SEB_RollCnt_Status` | 52 | 4 | u8 | Rolling counter echo |
| `SEB_CheckSum_Status`| 56 | 8 | u8 | XOR Checksum |

#### `0x206` MTR_MOTOR_FBK (Low Bus, 4 bytes, 50Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `MTR_ActualSpeed` | 0 | 16 | i16 | Actual motor speed (mm/s) |
| `MTR_GearState` | 16 | 8 | u8 | Actual gear (0=N, 1=D, 2=S, 3=R) |
| `MTR_FaultFlags` | 24 | 8 | u8 | bit0=ESTOP, bit1=CMD timeout, bit2=ADC fault, etc. |

#### `0x7FE` SYS_HEARTBEAT (Low Bus, 2 bytes, 10Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame |
| `health_flags` | 8 | 8 | u8 | bit0=hb_ok, bit1=estop_active |

### 1.4 CAN Messages - Transmitted (TX)

#### `0x204` RT_DRIVE_CMD (Low Bus, 5 bytes, 100Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `RT_MotorSpeed` | 0 | 32 | i32 | Target motor speed (mm/s) |
| `RT_Gear` | 32 | 8 | u8 | Target gear (0=N, 1=D, 2=S, 3=R) |

#### `0x205` RT_BRAKE_CMD (Low Bus, 4 bytes, 50Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `RT_BrakePressure`| 0 | 32 | i32 | Arbitrated target brake pressure (kPa) |

#### `0x169` VCU_SES_REQ (Low Bus, 8 bytes, 50Hz, Little Endian)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `VCU_SES_Align_En` | 0 | 1 | bool | Alignment Enable |
| `VCU_SES_Ctrl_En` | 1 | 1 | bool | Control Enable |
| `VCU_SES_Tgt_Angle`| 16 | 16 | i16 | Target Steering Angle |
| `VCU_SES_Tgt_Spd` | 32 | 16 | u16 | Target Steering Slew Rate (°/s) |
| `VCU_SES_RollCnt` | 44 | 4 | u8 | Rolling counter |
| `VCU_Veh_Spd_Value`| 48 | 8 | u8 | Current vehicle speed |
| `VCU_SES_CheckSum` | 56 | 8 | u8 | XOR Checksum |

#### `0x210` RT_STATE_RPT (High/Low Bus, 6 bytes, 10Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `RT_Mode` | 0 | 8 | u8 | 0=Manual, 1=Auto |
| `RT_SafetyState` | 8 | 2 | u8 | 0=Normal, 1=Int ESTOP, 2=Fault |
| `RT_EstopReason` | 12 | 4 | u8 | Reason code for ESTOP |
| `RT_Reversing` | 16 | 8 | bool | Is reversing |
| `RT_RxOverflow` | 24 | 8 | u8 | RX Queue overflow counter |
| `RT_TaskHealth` | 32 | 8 | u8 | Task health bitmask |
| `RT_SteerState` | 40 | 8 | u8 | Internal steering state machine status |

#### `0x7FD` RT_HEARTBEAT (High/Low Bus, 2 bytes, 2Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame |
| `health_flags` | 8 | 8 | u8 | bit0=hb_ok, bit1=estop_active, bit2=mode_auto, bit3=can_ok |

---

## 2. SYS Controller (Safety & Mode Authority)

### 2.1 Hardware Inputs & Outputs (GPIO)

| GPIO Pin | Direction | Function | Description |
|:---|:---|:---|:---|
| **4** | Input | TWAI RX | Low-Level CAN Bus receive |
| **5** | Output | TWAI TX | Low-Level CAN Bus transmit |
| **1** | Input | ESTOP Button | Normally Closed. Opens to 0V on ESTOP (active-low). |
| **2** | Input | Brake Lever | Handlebar brake lever (active-low, pull-up). |
| **11** | Input | Mode Button | Toggles MANUAL ↔ AUTO. |
| **41** | Input | START Button | Ignition, and overrides ESTOP back to Manual mode. |
| **9** | Input | Switch L-Turn | Left turn signal switch on handlebars. |
| **6** | Input | Switch R-Turn | Right turn signal switch on handlebars. |
| **7** | Input | Switch Headlght| Headlight switch on handlebars. |
| **17** | Output | Bulb READY | Green LED: System healthy & ready. |
| **20** | Output | Bulb ESTOP | Red LED: ESTOP currently active. |
| **48** | Output | Bulb AUTO | Auto mode indication LED. |
| **39** | Output | Bulb MANUAL | Manual mode indication LED. |
| **10** | Output | Relay Headlght | Drives 12V headlight. |
| **18** | Output | Relay Turn L | Drives 12V left turn signal. |
| **19** | Output | Relay Turn R | Drives 12V right turn signal. |
| **21** | Output | Relay Brake | Drives 12V brake light. |
| **40** | Output | Relay 12V Main | Controls non-safety 12V accessory power. |
| **47** | Output | WDT Toggle | Toggles an external TPS3850 Hardware Watchdog. |

### 2.2 Internal Data Structures & Synchronization

SYS state is separated into encapsulated class atomics.

#### Component Atomics
| Component | Atomic Variable | Type | Purpose |
|:---|:---|:---|:---|
| **ModeManager** | `m_mode` | `atomic<can::Mode>`| Authoritative vehicle mode (Manual, Auto, Estop). |
| **SafetyMonitor** | `m_estop` | `atomic<bool>` | Immediate reflection of GPIO1 state. |
| **SafetyMonitor** | `m_brake_lever`| `atomic<bool>` | Immediate reflection of GPIO2 state. |
| **SafetyMonitor** | `m_last_hb_us` | `atomic<int64_t>`| Timestamp of last valid `0x7FD` from RT. |
| **SafetyMonitor** | `m_hb_ever_seen`| `atomic<bool>` | Boot-sync flag. |
| **EGAS Monitor** | `m_last_setpt_tk`| `atomic<int64_t>`| Timestamp of last `0x204` command from RT. |

### 2.3 CAN Messages - Received (RX)

*SYS uses the same payload structures as defined above for `0x204 RT_DRIVE_CMD`, `0x206 MTR_MOTOR_FBK`, `0x721 SEB_STATUS`, and `0x7FD RT_HEARTBEAT`.*

#### `0x731` SEB_ErrInfo (Low Bus, 8 bytes, 10Hz, Little Endian)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `SEB_ECUUnderVolt` | 0 | 1 | bool | Controller Undervoltage |
| `SEB_CanCom_Err` | 2 | 1 | bool | CAN Communication Fault (L3) |
| `SEB_ECUTemp_Err` | 3 | 1 | bool | Controller Temperature Fault (L3) |
| `SEB_AngleSens_P_OC`| 7 | 1 | bool | Angle Sensor P Open Circuit (L3) |
| `SEB_Mtr_Stall_Err`| 17 | 1 | bool | Motor Stall Fault (L3) |
| *(Various Others)* | 8..25 | 1 | bool | Dozens of individual error bits |

### 2.4 CAN Messages - Transmitted (TX)

#### `0x001` SAFETY_ESTOP (Low Bus, 0 bytes, Event)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| *(No Payload)* | - | - | - | Presence of frame triggers immediate ESTOP. |

#### `0x110` SYS_MODE_CMD (Low Bus, 1 byte, Event+1s)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `SYS_Mode` | 0 | 8 | u8 | 0=MANUAL, 1=AUTO |

#### `0x011` SYS_SAFETY_STS (Low Bus, 3 bytes, 5Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `SYS_EstopActive` | 0 | 8 | u8 | 0=Normal, 1=ESTOP |
| `SYS_HeartbeatOk` | 8 | 8 | u8 | 0=Timeout, 1=OK |
| `SYS_LightLeft` | 16 | 1 | bool | Left turn indicator state |
| `SYS_LightRight` | 17 | 1 | bool | Right turn indicator state |
| `SYS_LightBrake` | 18 | 1 | bool | Brake light state |
| `SYS_LightHead` | 19 | 1 | bool | Headlight state |

#### `0x7B9` VCU_SEB_REQ (Low Bus, 8 bytes, 50Hz, Little Endian)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `VCU_SEB_Align_En` | 0 | 1 | bool | Calibration enable |
| `VCU_SEB_Ctrl_En` | 1 | 1 | bool | Active control enable |
| `VCU_SEB_Ctrl_Mode`| 2 | 1 | bool | 0=Stroke, 1=Pressure |
| `VCU_SEB_AutoBrake`| 3 | 1 | bool | Auto-brake emergency trigger |
| `VCU_SEB_Stroke` | 16 | 16 | u16 | Requested stroke position (mm) |
| `VCU_SEB_Pressure` | 24 | 8 | u8 | Requested pressure (MPa) (multiplexed with Stroke) |
| `VCU_SEB_RollCnt` | 52 | 4 | u8 | Rolling counter |
| `VCU_SEB_CheckSum` | 56 | 8 | u8 | XOR Checksum |

#### `0x600` SYS_DIAG_RPT (Low Bus, 8 bytes, 1Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `SYS_DiagMode` | 0 | 8 | u8 | Current mode |
| `SYS_DiagBrkEngage`| 8 | 1 | bool | Brake engaged flag |
| `SYS_DiagBrkFault` | 9 | 1 | bool | Brake fault flag |
| `SYS_DiagHbOk` | 16 | 1 | bool | RT heartbeat ok |
| `SYS_DiagEstopActv`| 24 | 8 | u8 | Estop reason/active |
| `SYS_DiagFreeHeap` | 32 | 16 | u16 | Free heap in KB |
| `SYS_DiagTec` | 48 | 8 | u8 | CAN TX Error Counter |
| `SYS_DiagRec` | 56 | 8 | u8 | CAN RX Error Counter |

#### `0x7FE` SYS_HEARTBEAT (Low Bus, 2 bytes, 10Hz)
| Signal Name | Start Bit | Length | Type | Description |
|:---|:---|:---|:---|:---|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame |
| `health_flags` | 8 | 8 | u8 | bit0=hb_ok, bit1=estop_active |
