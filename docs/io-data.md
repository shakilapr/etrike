# I/O Data — Complete Variable Inventory

This document catalogs every input and output variable across all four system components:
- **Host** (Jetson Orin) — high-level autonomy
- **RT** (ESP32-S3) — real-time physics, steering, CAN gateway
- **SYS** (ESP32-S3) — safety (EGAS Level 2), brake control, body control
- **MTR** (STM32) — dedicated motor controller (EGAS Level 1)

All CAN IDs and frame layouts are defined in `shared/can/can_protocol.h`. Physical pin mappings are in `docs/wiring.md`.

### Type Notation

| Notation | C Type | Meaning | Example |
|----------|--------|---------|---------|
| `i8` | `int8_t` | Signed 8-bit integer | — |
| `i16` | `int16_t` | Signed 16-bit integer | `i16 (mm/s)` — speed |
| `i24` | 24-bit signed (packed in CAN) | Signed 24-bit, non-standard width | `i24 (mrad/s)` — yaw rate |
| `i32` | `int32_t` | Signed 32-bit integer | `i32 (kPa)` — brake pressure |
| `u8` | `uint8_t` | Unsigned 8-bit integer | `u8 enum` — mode, gear |
| `u16` | `uint16_t` | Unsigned 16-bit integer | `u16 (0.05 mm/bit)` — stroke |
| `u32` | `uint32_t` | Unsigned 32-bit integer | `u32 (mm)` — distance |
| `u8 bool` | `uint8_t` | Boolean packed in a byte (0 or 1) | `SYS_EstopActive` |
| `u8 enum` | `uint8_t` | Enumeration packed in a byte | `{0=Manual, 1=Auto, 2=ESTOP}` |
| `u8 bitmask` | `uint8_t` | Bitfield, each bit is a flag | `fault_flags` |
| `DLC=0` | — | Zero-length CAN frame (no payload, event signal) | `SAFETY_ESTOP` |

### Physical Units

| Unit | Meaning | Used For | Concrete range |
|------|---------|----------|---------------|
| `mm/s` | Millimeters per second | Speed | [-500, 3000] = -0.5 to 3.0 m/s |
| `mrad/s` | Milliradians per second | Yaw rate | [-3000, 3000] |
| `kPa` | Kilopascals | Brake pressure | 0–20000 (20 MPa max) |
| `mm` | Millimeters | Distance | Obstacle: 0–4000 |
| `0.1°/bit` | Tenths of a degree per LSB | Steering angle | 455 = 45.5° |
| `0.05 mm/bit` | 0.05 mm per LSB | Brake stroke | -30 mm offset |
| `0.05 MPa/bit` | 0.05 MPa per LSB | Brake pressure | 1 bar = 0.1 MPa ≈ 2 bits |
| `°/s` | Degrees per second | Steering angle speed | EPS-C ramp rate |
| `Nm` | Newton-meters | Torque | Steering wheel resistance |

---

## 1. Host (Jetson Orin)

### 1.1 CAN Inputs — Host receives

| CAN ID | Message | Variable | Type | Range / Enum | Source |
|--------|---------|----------|------|-------------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | any node |
| `0x011` | SYS_SAFETY_STS | `SYS_EstopActive` | u8 bool | 0/1 | SYS → RT → Host |
| `0x011` | SYS_SAFETY_STS | `SYS_HeartbeatOk` | u8 bool | 0/1 | SYS → RT → Host |
| `0x120` | SYS_THROTTLE_STS | `SYS_ThrottleSpeed` | i16 (mm/s) | — | MTR → RT → Host |
| `0x210` | RT_STATE_RPT | `RT_Mode` | u8 enum | {0=Manual, 1=Auto, 2=ESTOP} | RT |
| `0x210` | RT_STATE_RPT | `RT_SteerValid` | u8 bool | 0/1 | RT |
| `0x210` | RT_STATE_RPT | `RT_Reversing` | u8 bool | 0/1 | RT |
| `0x220` | RT_PID_RPT | `RT_PidSetpoint` | i16 (mm/s) | — | RT (reserved) |
| `0x220` | RT_PID_RPT | `RT_PidMeasured` | i16 (mm/s) | — | RT (reserved) |
| `0x220` | RT_PID_RPT | `RT_PidOutput` | i16 | — | RT (reserved) |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagMode` | u8 | — | SYS → RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagBrakeEngaged` | u8 bool | 0/1 | SYS → RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagHeartbeatOk` | u8 bool | 0/1 | SYS → RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagEstopActive` | u8 bool | 0/1 | SYS → RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagFreeHeapKb` | u16 | — | SYS → RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagTec` | u8 | — | SYS → RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagRec` | u8 | — | SYS → RT → Host |
| `0x7FD` | RT_HEARTBEAT | `alive_ctr` | u8 | — | RT |

### 1.2 CAN Outputs — Host sends

| CAN ID | Message | Variable | Type | Range / Enum | Target |
|--------|---------|----------|------|-------------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | all |
| `0x300` | HOST_DRIVE_CMD | `HOST_DriveSpeed` | i32 (mm/s) | [-500, 3000] | RT |
| `0x300` | HOST_DRIVE_CMD | `HOST_YawRate` | i24 (mrad/s) | [-3000, 3000] | RT |
| `0x300` | HOST_DRIVE_CMD | `HOST_Gear` | u8 enum | {N=0, D=1, S=2, R=3} | RT |
| `0x301` | HOST_BRAKE_REQ | `HOST_BrakePressure` | i32 (kPa) | — | RT |
| `0x302` | HOST_LIGHT_CMD | `HOST_LeftTurn` | bool | 0/1 | RT |
| `0x302` | HOST_LIGHT_CMD | `HOST_RightTurn` | bool | 0/1 | RT |
| `0x302` | HOST_LIGHT_CMD | `HOST_BrakeLight` | bool | 0/1 | RT |
| `0x302` | HOST_LIGHT_CMD | `HOST_Headlight` | bool | 0/1 | RT |
| `0x400` | HOST_OBSTACLE_DIST | `HOST_ObstacleDistance` | u32 (mm) | — | RT |
| `0x7FC` | HOST_HEARTBEAT | `alive_ctr` | u8 | — | RT |

---

## 2. RT (ESP32-S3)

Role: real-time physics model, steering control (EPS-C via CAN), CAN gateway between high-side and low-side buses, obstacle braking, heartbeat/watchdog monitoring.

### 2.1 CAN Inputs — RT receives (low-side bus)

| CAN ID | Message | Variable | Type | Range / Enum | Source |
|--------|---------|----------|------|-------------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | any |
| `0x011` | SYS_SAFETY_STS | `SYS_EstopActive` | u8 bool | 0/1 | SYS |
| `0x011` | SYS_SAFETY_STS | `SYS_HeartbeatOk` | u8 bool | 0/1 | SYS |
| `0x110` | SYS_MODE_CMD | `SYS_Mode` | u8 enum | {0=Manual, 1=Auto, 2=ESTOP} | SYS |
| `0x120` | SYS_THROTTLE_STS | `SYS_ThrottleSpeed` | i16 (mm/s) | — | MTR |
| `0x201` | SES_STATUS | `SES_INF_Angle_Status` | bool | 0/1 | EPS-C |
| `0x201` | SES_STATUS | `SES_Control_Mode_Status` | u8 enum | — | EPS-C |
| `0x201` | SES_STATUS | `SES_Error_Status` | u8 enum | {0=N, 1=L1, 2=L2, 3=L3} | EPS-C |
| `0x201` | SES_STATUS | `SES_StrAngle` | i16 (0.1°/bit) | [-780, 780] | EPS-C |
| `0x201` | SES_STATUS | `EPS_SteeringWheel_Torq` | u8 (Nm) | — | EPS-C |
| `0x7FE` | SYS_HEARTBEAT | `alive_ctr` | u8 | — | SYS |

### 2.2 CAN Inputs — RT receives (high-side bus, via MCP2515)

| CAN ID | Message | Variable | Type | Range / Enum | Source |
|--------|---------|----------|------|-------------|--------|
| `0x300` | HOST_DRIVE_CMD | `HOST_DriveSpeed` | i32 (mm/s) | [-500, 3000] | Host |
| `0x300` | HOST_DRIVE_CMD | `HOST_YawRate` | i24 (mrad/s) | [-3000, 3000] | Host |
| `0x300` | HOST_DRIVE_CMD | `HOST_Gear` | u8 enum | {N,D,S,R} | Host |
| `0x301` | HOST_BRAKE_REQ | `HOST_BrakePressure` | i32 (kPa) | — | Host |
| `0x302` | HOST_LIGHT_CMD | light bits (4× bool) | u8 bitmask | — | Host |
| `0x7FC` | HOST_HEARTBEAT | `alive_ctr` | u8 | — | Host |

### 2.3 Physical Inputs — RT

| Variable | GPIO | Type | Connected To | Notes |
|----------|------|------|-------------|-------|
| `CAN_RX_LOW` | 4 | TWAI | SN65HVD230 RXD | Low-side CAN bus |
| `MCP2515_MISO` | 38 | SPI | MCP2515 SO | High-side CAN RX data |
| `MCP2515_INT` | 40 | Digital | MCP2515 INT | High-side CAN RX interrupt |
| `ENC_REAR_MOTOR_A` | 1 | PCNT | Motor encoder A | Speed feedback — active |
| `ENC_REAR_MOTOR_B` | 2 | PCNT | Motor encoder B | Quadrature phase B |
| `ENC_FRONT_WHEEL_A` | 3 | PCNT | Front wheel | Sensor not yet fitted |
| `ENC_FRONT_WHEEL_B` | 6 | PCNT | Front wheel | Sensor not yet fitted |
| `ENC_REAR_LEFT_A` | 9 | PCNT | Rear left wheel | Sensor not yet fitted |
| `ENC_REAR_LEFT_B` | 12 | PCNT | Rear left wheel | Sensor not yet fitted |
| `ENC_REAR_RIGHT_A` | 13 | PCNT | Rear right wheel | Sensor not yet fitted |
| `ENC_REAR_RIGHT_B` | 14 | PCNT | Rear right wheel | Sensor not yet fitted |
| `I2C_SDA` (IMU) | 10 | I2C | Optional IMU | — |

### 2.4 CAN Outputs — RT sends (low-side bus)

| CAN ID | Message | Variable | Type | Range / Enum | Rate | Target |
|--------|---------|----------|------|-------------|------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | event | all |
| `0x204` | RT_DRIVE_CMD | `RT_MotorSpeed` | i32 (mm/s) | [-500, 3000] | 100 Hz | MTR |
| `0x204` | RT_DRIVE_CMD | `RT_Gear` | u8 enum | {N,D,S,R} | 100 Hz | MTR |
| `0x205` | RT_BRAKE_CMD | `RT_BrakePressure` | i32 (kPa) | — | 50 Hz | SYS |
| `0x169` | VCU_SES_REQ | `VCU_SES_Alignment_Enable` | bool | 0/1 | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `VCU_SES_Control_Enable` | bool | 0/1 | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `VCU_SES_Control_Mode` | u8 | — | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `VCU_SES_Tgt_StrAngle` | i16 (0.1°/bit) | [-780, 780] | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `VCU_SES_Tgt_StrAngleSpd` | u16 (°/s) | — | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `roll_cnt_enable` | bool | must be 1 | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `checksum_enable` | bool | must be 1 | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `VCU_SES_RollCnt` | u8 (low 4 bits) | 0–15 rolling | 50 Hz | EPS-C |
| `0x169` | VCU_SES_REQ | `VCU_SES_CheckSum` | u8 | XOR over bytes 0–6 | 50 Hz | EPS-C |
| `0x302` | HOST_LIGHT_CMD (fwd) | light bits | u8 bitmask | — | on change | SYS |
| `0x7FD` | RT_HEARTBEAT | `alive_ctr` | u8 | — | 2 Hz | SYS |

### 2.5 CAN Outputs — RT sends (high-side bus, via MCP2515)

| CAN ID | Message | Variable | Type | Range / Enum | Rate | Target |
|--------|---------|----------|------|-------------|------|--------|
| `0x011` | SYS_SAFETY_STS (fwd) | `SYS_EstopActive` | u8 bool | 0/1 | 5 Hz | Host |
| `0x011` | SYS_SAFETY_STS (fwd) | `SYS_HeartbeatOk` | u8 bool | 0/1 | 5 Hz | Host |
| `0x120` | SYS_THROTTLE_STS (fwd) | `SYS_ThrottleSpeed` | i16 (mm/s) | — | 100 Hz | Host |
| `0x210` | RT_STATE_RPT | `RT_Mode` | u8 enum | {M,A,ESTOP} | 10 Hz | Host |
| `0x210` | RT_STATE_RPT | `RT_SteerValid` | u8 bool | 0/1 | 10 Hz | Host |
| `0x210` | RT_STATE_RPT | `RT_Reversing` | u8 bool | 0/1 | 10 Hz | Host |
| `0x220` | RT_PID_RPT | `RT_PidSetpoint` | i16 (mm/s) | — | reserved | Host |
| `0x220` | RT_PID_RPT | `RT_PidMeasured` | i16 (mm/s) | — | reserved | Host |
| `0x220` | RT_PID_RPT | `RT_PidOutput` | i16 | — | reserved | Host |
| `0x600` | SYS_DIAG_RPT (fwd) | 8-byte diagnostic struct | struct | — | 1 Hz | Host |
| `0x7FD` | RT_HEARTBEAT | `alive_ctr` | u8 | — | 2 Hz | Host |

### 2.6 Physical Outputs — RT

| Variable | GPIO | Type | Connected To | Notes |
|----------|------|------|-------------|-------|
| `CAN_TX_LOW` | 5 | TWAI | SN65HVD230 TXD | Low-side CAN bus |
| `SPI_SCK` | 36 | SPI | MCP2515 SCK | 10 MHz clock |
| `SPI_MOSI` | 37 | SPI | MCP2515 SI | High-side CAN TX data |
| `SPI_CS` | 39 | Digital | MCP2515 CS | Active-low chip select |
| `I2C_SCL` (IMU) | 11 | I2C | Optional IMU | Clock |
| `WDT_TOGGLE` | 21 | Digital | TPS3850 WDI | Toggled at 100 Hz by control task |

---

## 3. SYS (ESP32-S3)

Role: safety monitoring (EGAS Level 2), ESTOP handling, brake control (SEB via CAN), body control (lights, DCDC, mode management), watchdog. Motor actuation currently runs on SYS as fallback (MCP4725 DAC, throttle ADC, gear relays). Target: MTR STM32 for EGAS Level 1 isolation — see docs/mtr-migration.md. SYS also performs EGAS Level 2 monitoring via CAN 0x206 feedback.

### 3.1 CAN Inputs — SYS receives

| CAN ID | Message | Variable | Type | Range / Enum | Source |
|--------|---------|----------|------|-------------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | any |
| `0x205` | RT_BRAKE_CMD | `RT_BrakePressure` | i32 (kPa) | — | RT |
| `0x206` | MTR_MOTOR_FBK | `actual_speed_mmps` | i16 (mm/s) | — | MTR |
| `0x206` | MTR_MOTOR_FBK | `gear_state` | u8 enum | {N,D,S,R} | MTR |
| `0x206` | MTR_MOTOR_FBK | `fault_flags` | u8 bitmask | — | MTR |
| `0x302` | HOST_LIGHT_CMD | light bits (4× bool) | u8 bitmask | — | RT (fwd) |
| `0x721` | SEB_STATUS | `SEB_Alignment_Status` | bool | 0/1 | SEB |
| `0x721` | SEB_STATUS | `SEB_Control_Enable_Status` | bool | 0/1 | SEB |
| `0x721` | SEB_STATUS | `SEB_Control_Mode_Status` | u8 enum | — | SEB |
| `0x721` | SEB_STATUS | `SEB_Error_Status` | u8 enum | {0=N, 1=L1, 2=L2, 3=L3} | SEB |
| `0x721` | SEB_STATUS | `SEB_Stroke_Value` | u16 (0.05 mm/bit) | offset -30 mm | SEB |
| `0x721` | SEB_STATUS | `SEB_Pressure_Value` | u8 (0.05 MPa/bit) | — | SEB |
| `0x721` | SEB_STATUS | `SEB_RollCnt_Status` | u8 (low 4 bits) | — | SEB |
| `0x721` | SEB_STATUS | `SEB_CheckSum_Status` | u8 | — | SEB |
| `0x7FD` | RT_HEARTBEAT | `alive_ctr` | u8 | — | RT |

### 3.2 Physical Inputs — SYS

| Variable | GPIO | Type | Electrical | Notes |
|----------|------|------|-----------|-------|
| `CAN_RX` | 4 | TWAI | SN65HVD230 RXD | Low-side CAN bus |
| `ESTOP_BTN` | 1 | Digital NC | 10k pull-up to 3.3V, LOW = estop | Red mushroom button. Shared with MTR — only signal wired to both MCUs. |
| `BRAKE_LEVER` | 2 | Digital | Internal pull-up, LOW = pressed | Physical brake lever → SEB via CAN 0x7B9 |
| `START_BTN` | 32 | Digital | Internal pull-up, LOW = pressed | Green button, exits ESTOP |
| `MODE_BTN` | 11 | Digital | Internal pull-up, LOW = pressed | Manual/Auto toggle → publishes CAN 0x110 |
| `SW_LEFT_TURN` | 3 | Digital | Internal pull-up, LOW = pressed | Handlebar switch |
| `SW_RIGHT_TURN` | 6 | Digital | Internal pull-up, LOW = pressed | Handlebar switch |
| `SW_HEADLIGHT` | 7 | Digital | Internal pull-up, LOW = pressed | Handlebar switch |

### 3.3 CAN Outputs — SYS sends

| CAN ID | Message | Variable | Type | Range / Enum | Rate | Target |
|--------|---------|----------|------|-------------|------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | event | all |
| `0x011` | SYS_SAFETY_STS | `SYS_EstopActive` | u8 bool | 0/1 | 5 Hz | RT → Host |
| `0x011` | SYS_SAFETY_STS | `SYS_HeartbeatOk` | u8 bool | 0/1 | 5 Hz | RT → Host |
| `0x012` | SYS_DCDC_CMD | `SYS_DcdcEnable` | u8 bool | 0/1 | on change | DC-DC converter |
| `0x110` | SYS_MODE_CMD | `SYS_Mode` | u8 enum | {0=Manual, 1=Auto, 2=ESTOP} | on change | RT, MTR |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagMode` | u8 | — | 1 Hz | RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagBrakeEngaged` | u8 bool | 0/1 | 1 Hz | RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagHeartbeatOk` | u8 bool | 0/1 | 1 Hz | RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagEstopActive` | u8 bool | 0/1 | 1 Hz | RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagFreeHeapKb` | u16 | — | 1 Hz | RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagTec` | u8 | — | 1 Hz | RT → Host |
| `0x600` | SYS_DIAG_RPT | `SYS_DiagRec` | u8 | — | 1 Hz | RT → Host |
| `0x7B9` | VCU_SEB_REQ | (see §5.2) | — | — | 50 Hz | SEB (MANUAL/ESTOP only) |
| `0x7FE` | SYS_HEARTBEAT | `alive_ctr` | u8 | — | 10 Hz (100 ms) | RT |

### 3.4 Physical Outputs — SYS

| Variable | GPIO | Type | Connected To | Notes |
|----------|------|------|-------------|-------|
| `CAN_TX` | 5 | TWAI | SN65HVD230 TXD | Low-side CAN bus |
| `LIGHT_LEFT_TURN` | 18 | Digital | Relay → 12V lamp | Left turn signal |
| `LIGHT_RIGHT_TURN` | 19 | Digital | Relay → 12V lamp | Right turn signal |
| `BRAKE_LIGHT` | 21 | Digital | Relay → 12V lamp | Brake light |
| `HEADLIGHT` | 22 | Digital | Relay → 12V lamp | Low-beam headlight |
| `BULB_AUTO` | 25 | Digital | Relay → 12V indicator | Auto mode lamp |
| `BULB_MANUAL` | 26 | Digital | Relay → 12V indicator | Manual mode lamp |
| `RELAY_12V` | 27 | Digital | Relay → accessory bus | Cuts accessory power in ESTOP |
| `WDT_TOGGLE` | 23 | Digital | TPS3850 WDI | Toggled at 20 Hz by safety task |

---

## 4. MTR (STM32)

Role: dedicated motor controller (EGAS Level 1 — Function Controller). **Current status:** Task skeleton complete with correct state machines and CAN protocol handling. STM32 HAL driver layer (I2C, ADC, GPIO) is stubbed — migration from SYS pending. See docs/mtr-migration.md. Active in all modes — behavior gated by `0x110 SYS_MODE_CMD` from SYS.

### 4.0 Mode-Gated Behavior

| Mode | `0x110` | Throttle source | Gear source | Notes |
|------|:-------:|-----------------|-------------|-------|
| Manual | 0 | Read `THROTTLE_ADC` directly | Pass-through `GEAR_x_SENSE` → relays | Rider directly controls motor |
| Auto | 1 | Follow CAN `0x204` `RT_MotorSpeed` | Follow CAN `0x204` `RT_Gear` | RT kinematics drives motor |
| ESTOP | 2 | DAC = 0 (cut throttle) | All gear relays off | Hardware kill + CAN kill |

### 4.1 CAN Inputs — MTR receives

| CAN ID | Message | Variable | Type | Range / Enum | Source |
|--------|---------|----------|------|-------------|--------|
| `0x001` | SAFETY_ESTOP | (no payload) | DLC=0 | — | any |
| `0x110` | SYS_MODE_CMD | `SYS_Mode` | u8 enum | {0=M, 1=A, 2=E} | SYS |
| `0x204` | RT_DRIVE_CMD | `RT_MotorSpeed` | i32 (mm/s) | [-500, 3000] | RT |
| `0x204` | RT_DRIVE_CMD | `RT_Gear` | u8 enum | {N,D,S,R} | RT |
| `0x7FD` | RT_HEARTBEAT | `alive_ctr` | u8 | — | RT |

### 4.2 Physical Inputs — MTR

| Variable | Pin | Type | Electrical | Notes |
|----------|-----|------|-----------|-------|
| `CAN_RX` | TBD | CAN | CAN transceiver RXD | Low-side CAN bus |
| `ESTOP_BTN` | TBD | Digital NC | Shared with SYS GPIO1 | Hardwired Level 3 kill — zero CAN delay |
| `THROTTLE_ADC` | TBD | ADC | Voltage divider 5V→3.3V | Throttle grip 0–5V |
| `GEAR_D_SENSE` | TBD | Digital | TLP281 optocoupler ch1 | Gear D 72V sense |
| `GEAR_S_SENSE` | TBD | Digital | TLP281 optocoupler ch2 | Gear S 72V sense |
| `GEAR_R_SENSE` | TBD | Digital | TLP281 optocoupler ch3 | Gear R 72V sense |

### 4.3 CAN Outputs — MTR sends

| CAN ID | Message | Variable | Type | Range / Enum | Rate | Target |
|--------|---------|----------|------|-------------|------|--------|
| `0x120` | SYS_THROTTLE_STS | `actual_speed_mmps` | i16 (mm/s) | — | 100 Hz | RT (→ Host) |
| `0x206` | MTR_MOTOR_FBK | `actual_speed_mmps` | i16 (mm/s) | — | 50 Hz | SYS, RT |
| `0x206` | MTR_MOTOR_FBK | `gear_state` | u8 enum | {N,D,S,R} | 50 Hz | SYS, RT |
| `0x206` | MTR_MOTOR_FBK | `fault_flags` | u8 bitmask | bit0=ESTOP_ACTIVE | 50 Hz | SYS, RT |

### 4.4 Physical Outputs — MTR

| Variable | Pin | Type | Connected To | Notes |
|----------|-----|------|-------------|-------|
| `CAN_TX` | TBD | CAN | CAN transceiver TXD | Low-side CAN bus |
| `MCP4725_SDA` | TBD | I2C | MCP4725 DAC (addr 0x60) | Throttle 0–5V output |
| `MCP4725_SCL` | TBD | I2C | MCP4725 DAC | I2C clock |
| `GEAR_D_OUT` | TBD | Digital | Relay channel 1 | 72V to ECU gear D |
| `GEAR_S_OUT` | TBD | Digital | Relay channel 2 | 72V to ECU gear S |
| `GEAR_R_OUT` | TBD | Digital | Relay channel 3 | 72V to ECU gear R |

---

## 5. External Actuators (non-host CAN nodes)

### 5.1 SYNTREE EPS-C (Steering Actuator)

**CAN Inputs** (receives `0x169 VCU_SES_REQ` from RT):

| Variable | Type | Range |
|----------|------|-------|
| `VCU_SES_Alignment_Enable` | bool | 0/1 |
| `VCU_SES_Control_Enable` | bool | 0/1 |
| `VCU_SES_Tgt_StrAngle` | i16 (0.1°/bit) | [-780, 780] |
| `VCU_SES_Tgt_StrAngleSpd` | u8 (°/s) | — |
| `roll_cnt_enable` | bool | must be 1 |
| `checksum_enable` | bool | must be 1 |
| `VCU_SES_RollCnt` | u8 (low 4 bits) | 0–15 |
| `VCU_SES_CheckSum` | u8 | XOR(0..6) |

**CAN Outputs** (sends `0x201 SES_STATUS` to RT):

| Variable | Type | Range |
|----------|------|-------|
| `SES_INF_Angle_Status` | bool | 0/1 |
| `SES_Control_Mode_Status` | u8 enum | — |
| `SES_Error_Status` | u8 enum | {0=N, 1=L1, 2=L2, 3=L3} |
| `SES_StrAngle` | i16 (0.1°/bit) | — |
| `EPS_SteeringWheel_Torq` | u8 (Nm) | — |

### 5.2 SYNTREE SEB (Brake Actuator)

**CAN Inputs** (receives `0x7B9 VCU_SEB_REQ` — mode-gated sender: SYS in MANUAL/ESTOP, RT in AUTO):

| Variable | Type | Range |
|----------|------|-------|
| `VCU_SEB_Alignment_Enable` | bool | 0/1 |
| `VCU_SEB_Control_Enable` | bool | 0/1 |
| `VCU_SEB_Control_Mode` | u8 enum | {0=Stroke, 1=Pressure} |
| `VCU_SEB_AutoBrake` | bool | 0/1 |
| `VCU_SEB_Stroke_Value_Req` | u16 (0.05 mm/bit) | offset -30 mm |
| `VCU_SEB_Pre_Value_Req` | u8 (0.05 MPa/bit) | — |
| `VCU_SEB_RollCnt_Enable` | bool | must be 1 |
| `VCU_SEB_CheckSum_Enable` | bool | must be 1 |
| `VCU_SEB_RollCnt` | u8 (low 4 bits) | 0–15 |
| `VCU_SEB_CheckSum` | u8 | XOR(0..6) |

**CAN Outputs** (sends `0x721 SEB_STATUS` to SYS):

| Variable | Type | Range |
|----------|------|-------|
| `SEB_Alignment_Status` | bool | 0/1 |
| `SEB_Control_Enable_Status` | bool | 0/1 |
| `SEB_Control_Mode_Status` | u8 enum | — |
| `SEB_Error_Status` | u8 enum | {0=N, 1=L1, 2=L2, 3=L3} |
| `SEB_Stroke_Value` | u16 (0.05 mm/bit) | — |
| `SEB_Pressure_Value` | u8 (0.05 MPa/bit) | — |
| `SEB_RollCnt_Status` | u8 (low 4 bits) | — |
| `SEB_CheckSum_Status` | u8 | — |

### 5.3 DC-DC Converter

**CAN Inputs** (receives `0x012 SYS_DCDC_CMD` from SYS):

| Variable | Type | Range |
|----------|------|-------|
| `SYS_DcdcEnable` | u8 bool | 0/1 |

---

## 6. Data Flow Diagram

```
                         HIGH-SIDE CAN BUS (500 kbps)
 ┌──────────┐                                            ┌──────────┐
 │   HOST   │                                            │    RT    │
 │ (Jetson) │                                            │ (ESP32)  │
 │          │──0x300 DriveCmd───────────────────────────→│          │
 │          │──0x301 BrakeReq───────────────────────────→│          │
 │          │──0x302 LightCmd───────────────────────────→│          │
 │          │──0x7FC JetsonHB───────────────────────────→│          │
 │          │                                            │          │
 │          │←─0x011 SafetySts───────────────────────────│          │
 │          │←─0x120 ThrottleSts─────────────────────────│          │
 │          │←─0x210 StateRpt────────────────────────────│          │
 │          │←─0x220 PidRpt──────────────────────────────│          │
 │          │←─0x400 ObstacleRpt─────────────────────────│          │
 │          │←─0x600 DiagRpt─────────────────────────────│          │
 │          │←─0x7FD RTHB────────────────────────────────│          │
 └──────────┘                                            └─────┬────┘
                                                              │
                        LOW-SIDE CAN BUS (500 kbps)           │
       ┌──────────────────────────────────────────────────────┤
       │                                                      │
 ┌─────┴──────┐    ┌──────────┐    ┌──────────┐    ┌─────────┴──┐
 │    SYS     │    │   MTR    │    │  EPS-C   │    │    SEB     │
 │  (ESP32)   │    │ (STM32)  │    │(Steering)│    │  (Brake)  │
 │            │    │          │    │          │    │           │
 │→0x110 Mode─┼───→│ 0x110    │    │          │    │           │
 │→0x7B9 REQ──┼────┼──────────┼────┼──────────┼───→│←0x7B9────│
 │←0x721 STS──┼────┼──────────┼────┼──────────┼────│→0x721────│
 │            │    │          │    │←0x169────│    │           │
 │            │    │Motor DAC │    │→0x201────│    │           │
 │            │    │Gear rel. │    │          │    │           │
 │→Lights     │    │→0x120───→│    │          │    │           │
 │→DCDC 0x012 │    │→0x206───→│    │          │    │           │
 │→0x7FE HB──→│    │          │    │          │    │           │
 │←0x206 FBK──│    │          │    │          │    │           │
 └────────────┘    └──────────┘    └──────────┘    └───────────┘
```

### Key paths

| Path | Description |
|------|-------------|
| SYS → MTR (0x110) | Mode gating: Manual → MTR reads ADC/gear; Auto → MTR follows CAN 0x204; ESTOP → cut all |
| Host → RT → MTR (0x204) | Autonomous drive: speed + gear from RT kinematics → MTR DAC/relays |
| Host → RT → SEB (0x7B9) | AUTO brake: (2-hop via SYS; 1-hop RT→SEB planned per gap #12) |
| SYS → SEB (0x7B9) | MANUAL/ESTOP brake: brake lever → SYS GPIO2 → SEB CAN |
| RT → EPS-C (0x169) | Steering angle commands from RT to steering actuator |
| MTR → RT → Host (0x120) | Speed telemetry upstream for autonomy stack |
| MTR → SYS (0x206) | EGAS Level 2 monitoring: SYS compares 0x204 cmd vs 0x206 actual |
| SYS → RT → Host (0x011, 0x600) | Safety status and diagnostics forwarded through RT gateway |
