# CAN Signal Catalog — Per-Signal Test Matrix

**Total:** 37 messages, 169 signals

## 0x001 — SAFETY_ESTOP

- **Bus:** low | **DLC:** 0 | **Sender:** Any | **Cycle:** eventms
- **Receivers:** SYS, Host, MTR, DCDC
- **Comment:** DLC=0 — the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.

*(Event frame — no signals)*

## 0x001 — SAFETY_ESTOP

- **Bus:** high | **DLC:** 0 | **Sender:** Any | **Cycle:** eventms
- **Receivers:** SYS, Host, MTR, DCDC
- **Comment:** DLC=0 — the frame ID itself is the ESTOP signal. Any node can send (RT is nominal). Bridged bidirectionally. Highest priority CAN frame.

*(Event frame — no signals)*

## 0x011 — SYS_SAFETY_STS

- **Bus:** low | **DLC:** 3 | **Sender:** SYS | **Cycle:** 200ms
- **Receivers:** RT, Host
- **Comment:** Forwarded low→high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_EstopActive | 0 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_HeartbeatOk | 1 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_LightState | 2 | 0 | 4 | unsigned | [0, 15] | 1 | - | zero, max, min, mid |

## 0x011 — SYS_SAFETY_STS

- **Bus:** high | **DLC:** 3 | **Sender:** SYS | **Cycle:** 200ms
- **Receivers:** RT, Host
- **Comment:** Forwarded low→high by RT. Same payload on both buses. DLC=3 adds light state (v0.0.5).

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_EstopActive | 0 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_HeartbeatOk | 1 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_LightState | 2 | 0 | 4 | unsigned | [0, 15] | 1 | - | zero, max, min, mid |

## 0x012 — SYS_DCDC_CMD

- **Bus:** low | **DLC:** 1 | **Sender:** SYS | **Cycle:** eventms
- **Receivers:** DCDC
- **Comment:** DC-DC converter control. Low bus only.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_DcdcEnable | 0 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x110 — SYS_MODE_CMD

- **Bus:** low | **DLC:** 1 | **Sender:** SYS | **Cycle:** eventms
- **Receivers:** RT, MTR
- **Comment:** 0=Manual, 1=Auto, 2=ESTOP. Low bus only. MTR needs mode for pass-through vs CAN control.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_Mode | 0 | 0 | 8 | unsigned | [0, 2] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_Manual, enum_Auto, enum_ESTOP |

## 0x120 — SYS_THROTTLE_STS

- **Bus:** low | **DLC:** 2 | **Sender:** MTR | **Cycle:** 10ms
- **Receivers:** RT, Host
- **Comment:** Current vehicle speed from MTR STM32. Forwarded low→high by RT. SYS_ prefix is historical.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_ThrottleSpeed | 0 | 0 | 16 | signed | [-500, 3000] | 1 | mm/s | zero, max, min, mid, physical_min, physical_max |

## 0x120 — SYS_THROTTLE_STS

- **Bus:** high | **DLC:** 2 | **Sender:** MTR | **Cycle:** 10ms
- **Receivers:** RT, Host
- **Comment:** Current vehicle speed from MTR STM32. Forwarded low→high by RT. SYS_ prefix is historical.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_ThrottleSpeed | 0 | 0 | 16 | signed | [-500, 3000] | 1 | mm/s | zero, max, min, mid, physical_min, physical_max |

## 0x169 — VCU_SES_REQ

- **Bus:** low | **DLC:** 8 | **Sender:** RT | **Cycle:** 20ms
- **Receivers:** EPS_C
- **Comment:** steer-by-wire unit command. 50 Hz continuous. Byte 5 overlap: Speed[15:8] shares with security nibble per CSV.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SES_AlignEnable | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_CtrlEnable | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_TgtStrAngle | 2 | 0 | 16 | signed | [-700, 700] | 0.1 | deg | zero, max, min, mid, physical_min, physical_max |
| SES_TgtStrAngleSpd | 4 | 0 | 16 | unsigned | [125, 525] | 1 | deg/s | zero, max, min, mid, physical_min, physical_max |
| SES_RollCntEnable | 5 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_ChecksumEnable | 5 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_RollCnt | 5 | 4 | 4 | unsigned | [0, 15] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SES_VehSpd | 6 | 0 | 8 | unsigned | [0, 255] | 1 | km/h | zero, max, min, mid, physical_min, physical_max |
| SES_Checksum | 7 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x201 — SES_STATUS

- **Bus:** low | **DLC:** 8 | **Sender:** EPS_C | **Cycle:** 10ms
- **Receivers:** RT
- **Comment:** steer-by-wire unit status feedback. 100 Hz. Byte 5 overlap: StrAngleSpd[15:8] / Torq share byte 5 per CSV.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SES_AngleStatus | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_CtrlModeStatus | 0 | 1 | 2 | unsigned | [0, 3] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SES_ErrorStatus | 0 | 6 | 2 | unsigned | [0, 3] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_Normal, enum_L1_Warning, enum_L2_General, enum_L3_Severe |
| SES_StrAngle | 2 | 0 | 16 | unsigned | [-700, 700] | 0.1 | deg | zero, max, min, mid, physical_min, physical_max |
| SES_TgtStrAngleSpd_FB | 4 | 0 | 16 | signed | [0, 1480] | 0.5 | deg/s | zero, max, min, mid, physical_min, physical_max |
| SES_SteeringTorq | 5 | 0 | 8 | unsigned | [-12, 12] | 0.1 | Nm | zero, max, min, mid, physical_min, physical_max |
| SES_RollCntEnStatus | 6 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_ChecksumEnStatus | 6 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_RollCntStatus | 6 | 4 | 4 | unsigned | [0, 15] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SES_ChecksumStatus | 7 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x202 — SES_ErrInfo

- **Bus:** low | **DLC:** 8 | **Sender:** EPS_C | **Cycle:** 100ms
- **Receivers:** RT
- **Comment:** steer-by-wire unit detailed fault flags. 8 L3 faults (redundant sensor loss) -> RT must escalate to ESTOP.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SES_ECUUnderVolt | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_ECUOverVolt | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_CanComErr | 0 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_ECUTempErr | 0 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_DomainSC | 0 | 4 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_DomainV | 0 | 5 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_DomainT | 0 | 6 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_TempSensor | 0 | 7 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_AngleP_OC | 1 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_AngleP_AF | 1 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_AngleS_OC | 1 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_AngleS_AF | 1 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_SensorPow | 1 | 4 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_Alignment | 1 | 5 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_OverAngle | 1 | 6 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_StrMtrStall | 1 | 7 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_MtrCurtFault | 2 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_SensorCL | 2 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_TorqT1_OC | 2 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_TorqT1_AF | 2 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_TorqT2_OC | 2 | 4 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_TorqT2_AF | 2 | 5 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_SentAngle | 2 | 6 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_StrMtrIdling | 2 | 7 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_EPROM | 3 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SES_VehSpdSnapshot | 7 | 0 | 8 | unsigned | [0, 255] | 1 | km/h | zero, max, min, mid, physical_min, physical_max |

## 0x203 — SES_Version

- **Bus:** low | **DLC:** 8 | **Sender:** EPS_C | **Cycle:** 1000ms
- **Receivers:** RT
- **Comment:** steer-by-wire unit firmware version. Log on boot for compatibility check.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SES_SW_Version | 0 | 0 | 8 | unsigned | [0, 2.55] | 0.01 | - | zero, max, min, mid, physical_min, physical_max |
| SES_HW_Version | 1 | 0 | 8 | unsigned | [0, 25.5] | 0.1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x204 — RT_DRIVE_CMD

- **Bus:** low | **DLC:** 5 | **Sender:** RT | **Cycle:** 10ms
- **Receivers:** SYS, MTR
- **Comment:** MTR receives for motor actuation. SYS receives for EGAS L2 monitoring. ID 0x204 avoids collision with EPS-C 0x202.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| RT_MotorSpeed | 0 | 0 | 32 | signed | [-500, 3000] | 1 | mm/s | zero, max, min, mid, physical_min, physical_max |
| RT_Gear | 4 | 0 | 8 | unsigned | [0, 3] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_N, enum_D, enum_S, enum_R |

## 0x205 — RT_BRAKE_CMD

- **Bus:** low | **DLC:** 4 | **Sender:** RT | **Cycle:** 20ms
- **Receivers:** SYS
- **Comment:** RT max-select: max(rt_obstacle, host_0x301) -> SYS SEB cmd.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| RT_BrakePressure | 0 | 0 | 32 | signed | [0, 20000] | 1 | kPa | zero, max, min, mid, physical_min, physical_max |

## 0x206 — MTR_MOTOR_FBK

- **Bus:** low | **DLC:** 4 | **Sender:** MTR | **Cycle:** 20ms
- **Receivers:** RT, SYS, Host
- **Comment:** Motor feedback from STM32. Forwarded low→high by RT per gateway rules.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| MTR_ActualSpeed | 0 | 0 | 16 | signed | [-500, 3000] | 1 | mm/s | zero, max, min, mid, physical_min, physical_max |
| MTR_GearState | 2 | 0 | 8 | unsigned | [0, 3] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_N, enum_D, enum_S, enum_R |
| MTR_FaultFlags | 3 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max, enum_EstopActive, enum_CmdTimeout, enum_AdcFault, enum_GearConflict, enum_StartupReady |

## 0x206 — MTR_MOTOR_FBK

- **Bus:** high | **DLC:** 4 | **Sender:** MTR | **Cycle:** 20ms
- **Receivers:** RT, SYS, Host
- **Comment:** Motor feedback from STM32. Forwarded low→high by RT per gateway rules.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| MTR_ActualSpeed | 0 | 0 | 16 | signed | [-500, 3000] | 1 | mm/s | zero, max, min, mid, physical_min, physical_max |
| MTR_GearState | 2 | 0 | 8 | unsigned | [0, 3] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_N, enum_D, enum_S, enum_R |
| MTR_FaultFlags | 3 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max, enum_EstopActive, enum_CmdTimeout, enum_AdcFault, enum_GearConflict, enum_StartupReady |

## 0x210 — RT_STATE_RPT

- **Bus:** high | **DLC:** 4 | **Sender:** RT | **Cycle:** 100ms
- **Receivers:** Host
- **Comment:** RT state report to Host (high bus) and SYS (low bus). SYS monitors safety_state for takeover detection and RT health.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| RT_Mode | 0 | 0 | 8 | unsigned | [0, 2] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_Manual, enum_Auto, enum_ESTOP |
| RT_SafetyState | 1 | 0 | 2 | unsigned | [0, 2] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_Normal, enum_InternalEstop, enum_Fault |
| RT_Reversing | 2 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| RT_RxOverflow | 3 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x220 — RT_PID_RPT

- **Bus:** high | **DLC:** 6 | **Sender:** RT | **Cycle:** 100ms
- **Receivers:** Host
- **Comment:** RESERVED, inactive. PID telemetry for Host debugging.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| RT_PidSetpoint | 0 | 0 | 16 | signed | [-32768, 32767] | 1 | mm/s | zero, max, min, mid |
| RT_PidMeasured | 2 | 0 | 16 | signed | [-32768, 32767] | 1 | mm/s | zero, max, min, mid |
| RT_PidOutput | 4 | 0 | 16 | signed | [-32768, 32767] | 1 | - | zero, max, min, mid |

## 0x300 — HOST_DRIVE_CMD

- **Bus:** high | **DLC:** 8 | **Sender:** Host | **Cycle:** 10ms
- **Receivers:** RT
- **Comment:** Host (Jetson Orin) Autoware.Auto drive command -> RT. High bus only.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| HOST_DriveSpeed | 0 | 0 | 32 | signed | [-500, 3000] | 1 | mm/s | zero, max, min, mid, physical_min, physical_max |
| HOST_YawRate | 4 | 0 | 24 | signed | [-3000, 3000] | 1 | mrad/s | zero, max, min, mid, physical_min, physical_max |
| HOST_Gear | 7 | 0 | 8 | unsigned | [0, 3] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_N, enum_D, enum_S, enum_R |

## 0x301 — HOST_BRAKE_REQ

- **Bus:** high | **DLC:** 4 | **Sender:** Host | **Cycle:** eventms
- **Receivers:** RT
- **Comment:** On demand. RT arbitrates: max(RT_computed, HOST_request) -> 0x205.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| HOST_BrakePressure | 0 | 0 | 32 | signed | [0, 20000] | 1 | kPa | zero, max, min, mid, physical_min, physical_max |

## 0x302 — HOST_LIGHT_CMD

- **Bus:** low | **DLC:** 1 | **Sender:** Host | **Cycle:** eventms
- **Receivers:** RT, SYS
- **Comment:** Forwarded transparently high→low by RT.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| HOST_LeftTurn | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| HOST_RightTurn | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| HOST_BrakeLight | 0 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| HOST_Headlight | 0 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x302 — HOST_LIGHT_CMD

- **Bus:** high | **DLC:** 1 | **Sender:** Host | **Cycle:** eventms
- **Receivers:** RT, SYS
- **Comment:** Forwarded transparently high→low by RT.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| HOST_LeftTurn | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| HOST_RightTurn | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| HOST_BrakeLight | 0 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| HOST_Headlight | 0 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x310 — STEER_DIAG

- **Bus:** high | **DLC:** 8 | **Sender:** RT | **Cycle:** 100ms
- **Receivers:** Host
- **Comment:** Steering telemetry to Host. v0.0.4 — previously missing from DBC.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SteerDiag_Angle0_1deg | 0 | 0 | 16 | unsigned | [0, 65535] | 0.1 | deg | zero, max, min, mid |
| SteerDiag_Fault | 2 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SteerDiag_MotorCurrent | 3 | 0 | 16 | unsigned | [0, 65535] | 0.01 | A | zero, max, min, mid |
| SteerDiag_ECUTemp | 5 | 0 | 16 | unsigned | [0, 65535] | 0.1 | degC | zero, max, min, mid |
| SteerDiag_Reserved | 7 | 0 | 8 | unsigned | [0, 0] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x311 — BRAKE_DIAG

- **Bus:** high | **DLC:** 8 | **Sender:** RT | **Cycle:** 100ms
- **Receivers:** Host
- **Comment:** Brake telemetry to Host. v0.0.4 — previously missing from DBC.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| BrakeDiag_PressureRaw | 0 | 0 | 16 | unsigned | [0, 65535] | 0.05 | MPa | zero, max, min, mid |
| BrakeDiag_Fault | 2 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| BrakeDiag_MotorCurrent | 3 | 0 | 16 | unsigned | [0, 65535] | 0.01 | A | zero, max, min, mid |
| BrakeDiag_ECUTemp | 5 | 0 | 16 | unsigned | [0, 65535] | 0.1 | degC | zero, max, min, mid |
| BrakeDiag_Reserved | 7 | 0 | 8 | unsigned | [0, 0] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x400 — HOST_OBSTACLE_DIST

- **Bus:** high | **DLC:** 4 | **Sender:** Host | **Cycle:** 100ms
- **Receivers:** RT
- **Comment:** Host sends min obstacle distance (from LiDAR/camera perception) to RT at 10 Hz. High bus only.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| HOST_ObstacleDistance | 0 | 0 | 32 | unsigned | [0, 4294967295] | 1 | mm | zero, max, min, mid, physical_min, physical_max |

## 0x600 — SYS_DIAG_RPT

- **Bus:** low | **DLC:** 8 | **Sender:** SYS | **Cycle:** 1000ms
- **Receivers:** RT, Host
- **Comment:** SYS diagnostics report. Forwarded low→high by RT.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_DiagMode | 0 | 0 | 8 | unsigned | [0, 2] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagBrakeEngaged | 1 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagBrakeFault | 1 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagHeartbeatOk | 2 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagEstopActive | 3 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagFreeHeapKb | 4 | 0 | 16 | unsigned | [0, 65535] | 1 | KB | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagTec | 6 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagRec | 7 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x600 — SYS_DIAG_RPT

- **Bus:** high | **DLC:** 8 | **Sender:** SYS | **Cycle:** 1000ms
- **Receivers:** RT, Host
- **Comment:** SYS diagnostics report. Forwarded low→high by RT.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_DiagMode | 0 | 0 | 8 | unsigned | [0, 2] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagBrakeEngaged | 1 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagBrakeFault | 1 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagHeartbeatOk | 2 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagEstopActive | 3 | 0 | 8 | unsigned | [0, 1] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagFreeHeapKb | 4 | 0 | 16 | unsigned | [0, 65535] | 1 | KB | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagTec | 6 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SYS_DiagRec | 7 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x6FA — SES_Test

- **Bus:** low | **DLC:** 8 | **Sender:** EPS_C | **Cycle:** 10ms
- **Receivers:** RT
- **Comment:** steer-by-wire unit telemetry. 100 Hz. Bytes 0,7 reserved. Narrower ranges than brake SEB_Test.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SES_MtrCurt | 1 | 0 | 16 | signed | [0, 60] | 0.0078125 | A | zero, max, min, mid, physical_min, physical_max |
| SES_ECUTemp | 3 | 0 | 16 | unsigned | [0, 255] | 0.5 | degC | zero, max, min, mid, physical_min, physical_max |
| SES_PowVolt | 5 | 0 | 16 | unsigned | [0, 18] | 0.00390625 | V | zero, max, min, mid, physical_min, physical_max |

## 0x6FB — SEB_Test

- **Bus:** low | **DLC:** 8 | **Sender:** SEB | **Cycle:** 10ms
- **Receivers:** SYS, RT
- **Comment:** brake-by-wire unit telemetry. 100 Hz. RT monitors for 0x311 BRAKE_DIAG population.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SEB_MtrCurr | 1 | 0 | 16 | signed | [-255, 255] | 0.0078125 | A | zero, max, min, mid, physical_min, physical_max |
| SEB_ECUTemp | 3 | 0 | 16 | unsigned | [-40, 215] | 0.5 | degC | zero, max, min, mid, physical_min, physical_max |
| SEB_PowVolt | 5 | 0 | 16 | unsigned | [0, 32] | 0.00390625 | V | zero, max, min, mid, physical_min, physical_max |

## 0x721 — SEB_STATUS

- **Bus:** low | **DLC:** 8 | **Sender:** SEB | **Cycle:** 10ms
- **Receivers:** SYS, RT
- **Comment:** brake-by-wire unit status feedback. 100 Hz. RT monitors pressure for 0x311 BRAKE_DIAG. SYS usage: boot sync -> read StrokeValue; active -> confirm AlignStatus==1.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SEB_AlignStatus | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_CtrlEnStatus | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_CtrlModeStatus | 0 | 2 | 2 | unsigned | [0, 3] | 1 | - | zero, max, min, mid, physical_min, physical_max, enum_None, enum_Stroke, enum_Pressure |
| SEB_AutoBrakeStatus | 0 | 4 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_ErrorStatus | 0 | 6 | 2 | unsigned | [0, 3] | 1 | enum | zero, max, min, mid, physical_min, physical_max, enum_Normal, enum_L1_Warning, enum_L2_General, enum_L3_Severe |
| SEB_StrokeValue | 2 | 0 | 16 | unsigned | [-5, 27] | 0.05 | mm | zero, max, min, mid, physical_min, physical_max |
| SEB_PressureValue | 3 | 0 | 8 | unsigned | [0, 5] | 0.05 | MPa | zero, max, min, mid, physical_min, physical_max |
| SEB_AngleValue | 5 | 0 | 16 | signed | [-150, 840] | 0.5 | - | zero, max, min, mid, physical_min, physical_max |
| SEB_RollCntEnStatus | 6 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_ChecksumEnStatus | 6 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_RollCntStatus | 6 | 4 | 4 | unsigned | [0, 15] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SEB_ChecksumStatus | 7 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x731 — SEB_ErrInfo

- **Bus:** low | **DLC:** 8 | **Sender:** SEB | **Cycle:** 100ms
- **Receivers:** SYS
- **Comment:** brake-by-wire unit detailed fault flags. 16 of 23 faults are L3 -> SYS must escalate to ESTOP.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SEB_ECUUnderVolt | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_ECUOverVolt | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_CanComErr | 0 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_ECUTempErr | 0 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_DomainSC | 0 | 4 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_DomainV | 0 | 5 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_DomainT | 0 | 6 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_AngleP_OC | 0 | 7 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_AngleP_AF | 1 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_AngleS_OC | 1 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_AngleS_AF | 1 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_NoPreSensor | 1 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_SensorUCL | 1 | 5 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_AlignmentErr | 1 | 6 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_AngleOver | 1 | 7 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_MtrStall | 2 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_MtrDC | 2 | 2 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_OilErr | 2 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_InitOil | 2 | 4 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_SentValue | 2 | 5 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_MtrNoLoad | 2 | 6 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_PreSensorOver | 3 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_LowVoltCharging | 3 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |

## 0x741 — SEB_Version

- **Bus:** low | **DLC:** 8 | **Sender:** SEB | **Cycle:** 1000ms
- **Receivers:** SYS
- **Comment:** brake-by-wire unit firmware version. Log on boot for compatibility check.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SEB_SW_Version | 0 | 0 | 8 | unsigned | [0, 2.55] | 0.01 | - | zero, max, min, mid, physical_min, physical_max |
| SEB_HW_Version | 1 | 0 | 8 | unsigned | [0, 25.5] | 0.1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x7B9 — VCU_SEB_REQ

- **Bus:** low | **DLC:** 8 | **Sender:** SYS | **Cycle:** 20ms
- **Receivers:** SEB
- **Comment:** brake-by-wire unit brake command. 50 Hz continuous. Byte 3 mode-mux: Stroke[15:8] in Mode 0, Pressure in Mode 1.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SEB_AlignEnable | 0 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_CtrlEnable | 0 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_CtrlMode | 0 | 2 | 1 | unsigned | [0, 1] | 1 | enum | zero, max, min, mid, enum_Stroke, enum_Pressure |
| SEB_AutoBrake | 0 | 3 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_StrokeReq | 2 | 0 | 16 | unsigned | [-5, 27] | 0.05 | mm | zero, max, min, mid, physical_min, physical_max |
| SEB_PressureReq | 3 | 0 | 8 | unsigned | [0, 5] | 0.05 | MPa | zero, max, min, mid, physical_min, physical_max |
| SEB_RollCntEnable | 6 | 0 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_ChecksumEnable | 6 | 1 | 1 | unsigned | [0, 1] | 1 | - | zero, max, min, mid |
| SEB_RollCnt | 6 | 4 | 4 | unsigned | [0, 15] | 1 | - | zero, max, min, mid, physical_min, physical_max |
| SEB_Checksum | 7 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x7FC — HOST_HEARTBEAT

- **Bus:** high | **DLC:** 1 | **Sender:** Host | **Cycle:** 500ms
- **Receivers:** RT
- **Comment:** Not bridged, high bus only. Loss triggers controlled stop, not ESTOP.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| Host_AliveCtr | 0 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x7FD — RT_HEARTBEAT

- **Bus:** low | **DLC:** 2 | **Sender:** RT | **Cycle:** 500ms
- **Receivers:** Host, SYS
- **Comment:** RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the low-bus instance.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| RT_AliveCtr | 0 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x7FD — RT_HEARTBEAT

- **Bus:** high | **DLC:** 1 | **Sender:** RT | **Cycle:** 500ms
- **Receivers:** Host, SYS
- **Comment:** RT sends independently on both buses (per-bus, NOT bridged). Separate counters. This is the high-bus instance.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| RT_AliveCtr | 0 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

## 0x7FE — SYS_HEARTBEAT

- **Bus:** low | **DLC:** 2 | **Sender:** SYS | **Cycle:** 100ms
- **Receivers:** RT
- **Comment:** Low bus only, never leaves low bus.

| Signal | Byte | Bit | Size | Type | Range | Factor | Unit | Test Cases |
|---|---|---|---|---|---|---|---|---|
| SYS_AliveCtr | 0 | 0 | 8 | unsigned | [0, 255] | 1 | - | zero, max, min, mid, physical_min, physical_max |

