# CAN Signal Dictionary — E-Trike

Two physical CAN buses at 500 kbit/s. All fields big-endian (MSB first) unless noted (SYNTREE protocol uses Motorola LSB).

### Type Notation

| Notation | C Type | Meaning |
|----------|--------|---------|
| `i8 / i16 / i32` | `int8_t / int16_t / int32_t` | Signed integer |
| `i24` | 24-bit signed (packed) | Non-standard width, CAN frame only |
| `u8 / u16 / u32` | `uint8_t / uint16_t / uint32_t` | Unsigned integer |
| `u8 bool` | `uint8_t` | Boolean in a byte (0 or 1) |
| `u8 enum` | `uint8_t` | Enumeration in a byte |
| `u8 bitmask` | `uint8_t` | Bitfield, each bit is a flag |
| `DLC=0` | — | Zero-length CAN frame (event signal, no payload) |

### Physical Units

| Unit | Meaning | Example |
|------|---------|---------|
| `mm/s` | Millimeters per second | Speed: 3000 = 3.0 m/s |
| `mrad/s` | Milliradians per second | Yaw rate |
| `kPa` | Kilopascals | Brake pressure |
| `mm` | Millimeters | Distance |
| `0.1°/bit` | Tenths of a degree per LSB | Steer angle: 455 = 45.5° |
| `0.05 mm/bit` | 0.05 mm per LSB | Brake stroke |
| `0.05 MPa/bit` | 0.05 MPa per LSB | Brake pressure |
| `°/s` | Degrees per second | Steer angle speed |
| `Nm` | Newton-meters | Torque |

---

## 1. Low-Level CAN Bus

Nodes: RT ESP32-S3, SYS ESP32-S3, SYNTREE EPS-C (steering), SYNTREE SEB (brake), DC-DC converter.

---

### 0x001 — SAFETY_ESTOP

| Property | Value |
|----------|-------|
| **Sender** | Any (RT, SYS) |
| **Receiver(s)** | All nodes |
| **DLC** | 0 |
| **Period** | On event |

Presence of this frame = emergency stop. Motor stop, brake engage, steering disable, DCDC off.

---

### 0x011 — SYS_SAFETY_STS

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT (→ Jetson) |
| **DLC** | 2 |
| **Period** | 5 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_EstopActive` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — |
| `SYS_HeartbeatOk` | 8 | 8 | u8 | 1 | 0 | 0 | 1 | — | 0 = RT alive counter frozen >1000ms, 1 = alive counter incrementing |

---

### 0x012 — SYS_DCDC_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | DC-DC converter (72V→12V) |
| **DLC** | 1 |
| **Period** | On change |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_DcdcEnable` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — |

ESTOP → 0 (off). All other modes → 1 (on).

---

### 0x110 — SYS_MODE_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT |
| **DLC** | 1 |
| **Period** | On change |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_Mode` | 0 | 8 | u8 | 1 | 0 | 0 | 2 | enum (0=M, 1=A, 2=ESTOP) |

---

### 0x120 — SYS_THROTTLE_STS

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT (→ Jetson) |
| **DLC** | 2 |
| **Period** | 100 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_ThrottleSpeed` | 0 | 16 | i16 | 1 | 0 | 0 | 3000 | mm/s |

---

### 0x204 — RT_DRIVE_CMD

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | SYS |
| **DLC** | 5 |
| **Period** | 100 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `RT_MotorSpeed` | 0 | 32 | i32 | 1 | 0 | -500 | 3000 | mm/s |
| `RT_Gear` | 32 | 8 | u8 | 1 | 0 | 0 | 3 | enum (0=N,1=D,2=S,3=R) |

Byte layout (big-endian): Byte 0-3 = speed [31:0], Byte 4 = gear.

> Placed at `0x204` to avoid collision with EPS-C error info frame at `0x202`. SYNTREE units are preprogrammed and cannot be reconfigured.

---

### 0x205 — RT_BRAKE_CMD

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | SYS |
| **DLC** | 4 |
| **Period** | 50 Hz (20 ms) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `RT_BrakePressure` | 0 | 32 | i32 | 1 | 0 | 0 | 20000 | kPa |

Byte layout (big-endian): Bytes 0-3 = brake pressure [kPa].

RT max-select: `brake_kpa = max(rt_obstacle, jetson_0x301)`. SYS converts: `seb_raw = (uint8_t)(kpa * 0.02f)` (verified SYNTREE spec: `VCU_SEB_Pre_Value_Req` is u8, scale 0.05 MPa/bit, range 0–5 MPa). When `0x205 > 0`, SYS switches SEB to Pressure Mode (mode=2). When `0x205 == 0`, falls back to Stroke Mode for lever/ESTOP triggers.


### 0x201 — SES_STATUS (SYNTREE EPS-C Feedback)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE EPS-C (steering module) |
| **Receiver(s)** | RT |
| **DLC** | 8 |
| **Period** | 10 ms (100 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SES_INF_Angle_Status` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Center Finding Status. 0=Center Finding, 1=Found. (CSV Row 11) |
| `SES_Control_Mode_Status` | 1 | 2 | u8 | 1 | 0 | 0 | 3 | enum | Control Mode Feedback. 0=Manual, 1=Automatic. (CSV Row 12) |
| (unaccounted) | 3 | 3 | — | — | — | — | — | — | Byte 0 bits 3–5 — not enumerated in CSV |
| `SES_Error_Status` | 6 | 2 | u8 | 1 | 0 | 0 | 3 | enum | Error Status. 0=Normal, 1=L1 Warning, 2=L2, 3=L3. (CSV Row 13) |
| (unaccounted) | — | 8 | — | — | — | — | — | — | Byte 1 — not enumerated in CSV |
| `SES_StrAngle` | 16 | 16 | u16 | 0.1 | -3000 | -700 | 700 | ° | Steering Angle. Unsigned per CSV. Raw 0→-3000°, raw 30000→0°, raw 23000→-700°, raw 37000→700°. (CSV Row 14) |
| `SES_Tgt_StrAngleSpd` | 32 | 16 | i16 | 0.5 | 0 | 0 | 1480 | °/s | Target Angle Speed. 16-bit signed per CSV. Overlaps Torq at byte 5. (CSV Row 15) |
| `EPS_SteeringWheel_Torq` | 40 | 8 | u8 | 0.1 | -12.1 | -12 | 12 | Nm | Steering Wheel Torque Feedback. Overlaps StrAngleSpd[15:8] at byte 5. Init 0x79 (121 raw = 0 Nm). (CSV Row 16) |
| `SES_RollCnt_Enable_Status` | 48 | 1 | bool | 1 | 0 | 0 | 1 | — | Life Signal Enable Feedback. 0=Invalid, 1=Valid. (CSV Row 17) |
| `SES_CheckSum_Enable_Status` | 49 | 1 | bool | 1 | 0 | 0 | 1 | — | Checksum Enable Feedback. 0=Invalid, 1=Valid. (CSV Row 18) |
| (unaccounted) | 50 | 2 | — | — | — | — | — | — | Byte 6 bits 2–3 — not enumerated in CSV |
| `SES_RollCnt_Status` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Life Signal Feedback. Rolling counter 0–15. (CSV Row 19) |
| `SES_CheckSum_Status` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum Feedback = XOR(bytes 0–6) ^ 0xFF. (CSV Row 20) |

**Byte layout** (little-endian, CSV as source of truth):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | AngleSts[0]+ModeSts[1:2]+(gap)+Error[6:7] | (unacc.) | StrAngle [7:0] | StrAngle [15:8] | Speed [7:0] | Speed[15:8] / Torq [7:0] (overlap) | RollCntEn[0]+CksEn[1]+(gap)+RollCnt[4:7] | CksSum_Stat |

> **StrAngle conversion (CSV Unsigned, offset=-3000):** `physical_deg = raw × 0.1 − 3000`. The EPS-C encodes steering angle as an unsigned 16-bit value with -3000 offset. Raw 30000 → 0° (straight). Raw 23000 → -700° (full left). Raw 37000 → 700° (full right). In practice, RT uses `internal_angle_mdeg / 100` to produce the raw value; adjust per actual calibration.
>
> **Torq encoding (CSV scale=0.1, offset=-12.1):** `physical_Nm = raw × 0.1 − 12.1`. Raw 121 (0x79) → 0.0 Nm (no torque). Raw 0 → -12.1 Nm. Raw 241 → 12.0 Nm. The EPS-C biases torque readings so zero torque is at raw 121.
>
> **Byte 5 overlap:** CSV declares `SES_Tgt_StrAngleSpd` as 16-bit (bytes 4–5, Signed) AND `EPS_SteeringWheel_Torq` as 8-bit (byte 5). Both are listed — the EPS-C may report these in alternate frames or the CSV represents signals available across firmware versions.

---

### 0x169 — VCU_SES_REQ (SYNTREE EPS-C Command)

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | SYNTREE EPS-C (steering module) |
| **DLC** | 8 |
| **Period** | 20 ms (50 Hz) — **continuous, every frame** |
| **Endianness** | Motorola LSB (little-endian) |
| **Note** | Factory default `0x169`. SYNTREE unit is preprogrammed and not reconfigurable. `RT_DRIVE_CMD` placed at `0x204` to avoid collision. |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `VCU_SES_Alignment_Enable` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | SES Angle Initial Alignment Enable. 0=disabled, 1=centering. (CSV Row 2) |
| `VCU_SES_Control_Enable` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | VCU Direction Control Enable. 0=Disabled (Default Assist), 1=Rising Edge Enable (Angle Control Mode). (CSV Row 3) |
| (unaccounted) | 2 | 6 | — | — | — | — | — | — | Byte 0 bits 2–7 — not enumerated in CSV |
| (unaccounted) | — | 8 | — | — | — | — | — | — | Byte 1 — not enumerated in CSV |
| `VCU_SES_Tgt_StrAngle` | 16 | 16 | i16 | 0.1 | -3000 | -700 | 700 | ° | Target Steering Angle. Negative = left. (CSV Row 4). Note: CSV offset=-3000 (see conversion note below). |
| `VCU_SES_Tgt_StrAngleSpd` | 32 | 16 | u16 | 1 | 0 | 125 | 525 | °/s | Target Steering Angle Speed. 16-bit per CSV. Overlaps security signals at byte 5. (CSV Row 5) |
| `VCU_SES_RollCnt_Enable` | 40 | 1 | bool | 1 | 0 | 0 | 1 | — | Life Signal Enable — **Must be 1**. Overlaps StrAngleSpd[15:8]. (CSV Row 6) |
| `VCU_SES_CheckSum_Enable` | 41 | 1 | bool | 1 | 0 | 0 | 1 | — | Checksum Enable — **Must be 1**. Overlaps StrAngleSpd[15:8]. (CSV Row 7) |
| (unaccounted) | 42 | 2 | — | — | — | — | — | — | Byte 5 bits 2–3 — not enumerated in CSV |
| `VCU_SES_RollCnt` | 44 | 4 | u8 | 1 | 0 | 0 | 15 | — | Life Signal rolling counter. Increment every frame. Overlaps StrAngleSpd[15:8]. (CSV Row 8) |
| `VCU_Veh_Spd_Value` | 48 | 8 | u8 | 1 | 0 | 0 | 255 | — | Vehicle Speed. RT must populate with current speed. (CSV Row 9) |
| `VCU_SES_CheckSum` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum = XOR(bytes 0–6) ^ 0xFF (CSV Row 10) |

**Byte layout** (little-endian, CSV as source of truth):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | Align[0]+CtrlEn[1] | (unacc.) | Angle [7:0] | Angle [15:8] | Speed [7:0] | Speed[15:8] / RollCntEn[0]+CksEn[1]+(gap)+RollCnt[4:7] (overlap) | Veh_Spd [7:0] | CheckSum |

> **Angle conversion (CSV offset=-3000):** `physical_deg = raw × 0.1 + (-3000)`. This encoding places the ±700° physical range at raw values approximately 23000–37000. Raw 0 → -3000° (outside normal range). This offset is unusual for a signed integer — 0° does not map to raw 0. The CSV offset may be a tool artifact; verify against observed CAN bus values.
>
> **Byte 5 overlap:** CSV declares `VCU_SES_Tgt_StrAngleSpd` as 16-bit (bytes 4–5) AND security signals at byte 5 (RollCnt_Enable, CheckSum_Enable, RollCnt). Both are listed in the manufacturer's DBC export. The EPS-C may internally separate these — the upper nibble of byte 5 carries security data while the lower bits carry speed. RT firmware should write the speed value to bytes 4–5 AND set security fields at byte 5 bits 0–3 + upper nibble; the EPS-C validates the security portion independently of the speed portion.
>
> **Slew rate:** Speed-dependent. RT computes `VCU_SES_Tgt_StrAngleSpd` based on speed to ensure smooth steering. CSV lists range 125–525 °/s. The EPS-C may reject speed commands below 125 °/s.

**Internal conversion (architecture, offset=0)**: `VCU_SES_Tgt_StrAngle_raw = internal_angle_mdeg / 100` (45500 mdeg → 455 raw → 45.5°). CSV declares offset=-3000; if that encoding is used, the formula would be `raw = internal_angle_mdeg / 100 + 30000` (45500 mdeg → 30455 raw → 45.5°). Verify which encoding the EPS-C actually expects by observing CAN bus traffic.

**Security**: If `roll_cnt_enable=0` or `checksum_enable=0`, unit may reject frames. Both must be 1. Checksum algorithm: `XOR(bytes[0..6]) ^ 0xFF` (verify exact formula against SYNTREE spec).

**Slew rate**: Speed-dependent. RT computes `VCU_SES_Tgt_StrAngleSpd` based on speed to ensure smooth steering. Lower speed → lower slew rate for comfort; higher speed → higher slew rate for responsiveness (within dynamic clamp).

---

### 0x202 — SES_ErrInfo (SYNTREE EPS-C Error Detail)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE EPS-C (steering module) |
| **Receiver(s)** | RT ESP32-S3 |
| **DLC** | 8 |
| **Period** | 100 ms (10 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

Detailed fault flags. Each bit is an independent fault indicator. 1 = fault active.

| Signal | Start bit | Len | Type | Fault Level | Description |
|--------|-----------|-----|------|-------------|-------------|
| `SES_ECUUnderVolt_Err` | 0 | 1 | bool | L2 | Controller Under Voltage |
| `SES_ECUOverVolt_Err` | 1 | 1 | bool | L2 | Controller Over Voltage |
| `SES_CanCom_Err` | 2 | 1 | bool | L1 | CAN Communication Fault |
| `SES_ECUTemp_Err` | 3 | 1 | bool | L1 | Controller Temp Fault |
| `SES_Domain_drive_SC_Err` | 4 | 1 | bool | L2 | Domain Drive Short Circuit |
| `SES_Domain_drive_V_Err` | 5 | 1 | bool | L2 | Domain Drive Voltage Fault |
| `SES_Domain_drive_T_Err` | 6 | 1 | bool | L2 | Domain Drive Temperature Fault |
| `SES_TempSensor_Err` | 7 | 1 | bool | — | Temperature Sensor Fault |
| `SES_AngleSensor_P_OC_Err` | 8 | 1 | bool | **L3** | Angle Sensor Pri. Open Circuit |
| `SES_AngleSensor_P_AF_Err` | 9 | 1 | bool | **L3** | Angle Sensor Pri. Out of Range |
| `SES_AngleSensor_S_OC_Err` | 10 | 1 | bool | **L3** | Angle Sensor Sec. Open Circuit |
| `SES_AngleSensor_S_AF_Err` | 11 | 1 | bool | **L3** | Angle Sensor Sec. Out of Range |
| `SES_SensorPow_Err` | 12 | 1 | bool | L2 | Sensor Power Fault |
| `SES_Alignment_Err` | 13 | 1 | bool | L1 | Centering Fault |
| `SES_OverAngle_Err` | 14 | 1 | bool | L2 | Over Angle Fault |
| `SES_StrMtr_Stall_Err` | 15 | 1 | bool | L1 | Motor Stall Fault |
| `SES_MtrCurt_Err` | 16 | 1 | bool | L2 | Motor Current Fault |
| `SES_SensorCL_Err` | 17 | 1 | bool | L2 | Sensor 5V Power 1/2 Fault |
| `SES_TorqSensor_T1_OC_Err` | 18 | 1 | bool | **L3** | Torque Sensor T1 Open Circuit |
| `SES_TorqSensor_T1_AF_Err` | 19 | 1 | bool | **L3** | Torque Sensor T1 Out of Range |
| `SES_TorqSensor_T2_OC_Err` | 20 | 1 | bool | **L3** | Torque Sensor T2 Open Circuit |
| `SES_TorqSensor_T2_AF_Err` | 21 | 1 | bool | **L3** | Torque Sensor T2 Out of Range |
| `SentAngle_Err` | 22 | 1 | bool | L1 | Angle Error |
| `SES_StrMtr_Idling_Err` | 23 | 1 | bool | L2 | Motor Idling Fault |
| `SES_EPROM_Err` | 24 | 1 | bool | L2 | EEPROM Fault |
| (reserved) | 25 | 31 | — | — | Bits 25–55 (byte 3 bits 1–7 + bytes 4–6). Not enumerated in CSV. |
| `SES_Veh_Spd_Value` | 56 | 8 | u8 | — | Vehicle speed at fault snapshot (CSV Row 46) |

**Byte layout** (little-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | faults [7:0] | faults [15:8] | faults [23:16] | faults [31:24] | rsvd | rsvd | rsvd | `SES_Veh_Spd_Value` |

> **Safety note:** 6 faults are L3 (angle sensor primary/secondary open-circuit/out-of-range + torque sensor T1/T2 faults). RT must subscribe and escalate L3 faults to ESTOP. The EPS-C has dual-redundant angle sensors (P/S) and dual torque sensors (T1/T2) — L3 on either channel of a redundant pair indicates critical sensor failure. Note fault levels differ from SEB: steering CAN comms is L1 (minor) vs brake CAN comms L3 (severe) — steering comm loss is less immediately dangerous than brake comm loss.

> ⚠️ **CSV Row 40–41 description swap:** Row 40 signal `SES_TorqSensor_T2_OC_Err` is labeled "Torque Sensor T1 Out of Range" — descriptions appear swapped. Signal names used above are authoritative; descriptions are corrected.

---

### 0x203 — SES_Version (SYNTREE EPS-C Firmware Version)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE EPS-C (steering module) |
| **Receiver(s)** | RT ESP32-S3 |
| **DLC** | 8 |
| **Period** | 1000 ms (1 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SES_SW_Version` | 0 | 8 | u8 | 0.01 | 0 | 0 | 255 | — | Software version (e.g., 0x64 = 1.00) |
| `SES_HW_Version` | 8 | 8 | u8 | 0.1 | 0 | 0 | 25.5 | — | Hardware version (e.g., 0x0D = 1.3) |
| (reserved) | 16 | 48 | — | — | — | — | — | — | Bytes 2–7 |

**RT usage**: Log on boot for compatibility check. Report via telemetry to Jetson.

---

### 0x6FA — SES_Test (SYNTREE EPS-C Telemetry)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE EPS-C (steering module) |
| **Receiver(s)** | RT ESP32-S3 |
| **DLC** | 8 |
| **Period** | 10 ms (100 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| (reserved) | 0 | 8 | — | — | — | — | — | — | Byte 0 |
| `SES_MtrCurt` | 8 | 16 | i16 | 0.0078125 | 0 | 0 | 60 | A | Motor current. Bytes 1–2. Narrower range than brake SEB_Test (±255A). |
| `SES_ECUTemp` | 24 | 16 | u16 | 0.5 | 0 | 0 | 255 | °C | ECU temperature. Bytes 3–4. |
| `SES_PowVolt` | 40 | 16 | u16 | 0.00390625 | 0 | 0 | 18 | V | Supply voltage. Bytes 5–6. Narrower range than brake (0–18V vs 0–32V). |
| (reserved) | 56 | 8 | — | — | — | — | — | — | Byte 7 |

> **Note:** CSV uses byte-local start-bit numbering for this message. Converted to absolute Motorola LSB above. Range limits differ from brake SEB_Test (0x6FB): steering motor current 0–60A vs brake ±255A; steering supply voltage 0–18V vs brake 0–32V. **RT usage:** Monitor `SES_MtrCurt` for mechanical binding / rack damage. `SES_ECUTemp` for thermal throttling.

---

### 0x302 — HOST_LIGHT_CMD (forwarded)

| Property | Value |
|----------|-------|
| **Sender** | RT (fwd from Jetson) |
| **Receiver(s)** | SYS |
| **DLC** | 1 |
| **Period** | On change |

| Signal | Start bit | Len | Type |
|--------|-----------|-----|------|
| `HOST_LeftTurn` | 0 | 1 | bool |
| `HOST_RightTurn` | 1 | 1 | bool |
| `HOST_BrakeLight` | 2 | 1 | bool |
| `HOST_Headlight` | 3 | 1 | bool |
| (reserved) | 4 | 4 | — |

---

### 0x600 — SYS_DIAG_RPT

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT (→ Jetson) |
| **DLC** | 8 |
| **Period** | 1 Hz |

| Signal | Start bit | Len | Type |
|--------|-----------|-----|------|
| `SYS_DiagMode` | 0 | 8 | u8 |
| `SYS_DiagBrakeEngaged` | 8 | 8 | u8 |
| `SYS_DiagHeartbeatOk` | 16 | 8 | u8 |
| `SYS_DiagEstopActive` | 24 | 8 | u8 |
| `SYS_DiagFreeHeapKb` | 32 | 16 | u16 |
| `SYS_DiagTec` | 48 | 8 | u8 |
| `SYS_DiagRec` | 56 | 8 | u8 |

Byte layout (big-endian): Byte 0=mode, 1=brake, 2=hb_ok, 3=estop, 4-5=heap, 6=tec, 7=rec.

---

### 0x7B9 — VCU_SEB_REQ (SYNTREE SEB Brake Command)

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | SYNTREE SEB (brake module) |
| **DLC** | 8 |
| **Period** | 20 ms (50 Hz) — **continuous, every frame** |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `VCU_SEB_Alignment_Enable` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Calibration enable (CSV Row 2) |
| `VCU_SEB_Control_Enable` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Active control enable (CSV Row 3) |
| `VCU_SEB_Control_Mode` | 2 | 1 | bool | 1 | 0 | 0 | 1 | enum | 0=Stroke, 1=Pressure (CSV Row 4) |
| `VCU_SEB_AutoBrake` | 3 | 1 | bool | 1 | 0 | 0 | 1 | — | Auto-brake / emergency trigger (CSV Row 5) |
| (unaccounted) | 4 | 4 | — | — | — | — | — | — | Byte 0 bits 4–7 — not enumerated in CSV |
| (unaccounted) | — | 8 | — | — | — | — | — | — | Byte 1 — not enumerated in CSV |
| `VCU_SEB_Stroke_Value_Req` | 16 | 16 | u16 | 0.05 | -30 | -5 | 27 | mm | Requested stroke position (CSV Row 6) |
| `VCU_SEB_Pre_Value_Req` | 24 | 8 | u8 | 0.05 | 0 | 0 | 5 | MPa | Requested pressure (CSV Row 7). Raw = kPa × 0.02. Overlaps Stroke[15:8] at byte 3 — mode-dependent: Stroke uses full 16-bit in Mode 0, byte 3 carries pressure in Mode 1. |
| (unaccounted) | 32 | 16 | — | — | — | — | — | — | Bytes 4–5 — not enumerated in CSV |
| `VCU_SEB_RollCnt_Enable` | 48 | 1 | bool | 1 | 0 | 0 | 1 | — | Life Signal Validity — **Must be 1** (CSV Row 8) |
| `VCU_SEB_CheckSum_Enable` | 49 | 1 | bool | 1 | 0 | 0 | 1 | — | Checksum Validity — **Must be 1** (CSV Row 9) |
| (unaccounted) | 50 | 2 | — | — | — | — | — | — | Byte 6 bits 2–3 — not enumerated in CSV |
| `VCU_SEB_RollCnt` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Life Signal rolling counter. Increment every frame. (CSV Row 10) |
| `VCU_SEB_CheckSum` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum = XOR(bytes 0–6) ^ 0xFF (CSV Row 11) |

**Byte layout** (little-endian, CSV as source of truth):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | Align[0]+CtrlEn[1]+Mode[2]+AutoBrk[3] | (unaccounted) | Stroke_Req [7:0] | Stroke_Req [15:8] / Pre_Req [7:0] (mode-muxed) | (unaccounted) | (unaccounted) | RollCntEn[0]+CksEn[1]+(gap)+RollCnt[4:7] | CheckSum |

> **Byte 3 multiplexing:** In Stroke Mode (Mode=0), bytes 2–3 carry the 16-bit stroke value (`VCU_SEB_Stroke_Value_Req`). In Pressure Mode (Mode=1), byte 3 carries the 8-bit pressure value (`VCU_SEB_Pre_Value_Req`). Both signals are declared in the CSV at overlapping positions — the SEB interprets byte 3 based on the active mode bit. Bytes 1, 4, and 5 are not enumerated in the CSV; the SEB may ignore them or use them for undocumented functions.

**Stroke conversion**: `raw = (physical_mm + 30.0) / 0.05`

| Physical | Raw | Use case |
|----------|-----|----------|
| -5 mm | 500 | Min |
| 0 mm | 600 | Released |
| 15 mm | 900 | Manual lever pressed |
| 27 mm | 1140 | ESTOP full brake |

**Security**: Rolling counter must increment 0→15 every frame. Same value twice → SEB rejects (assumes frozen controller). Checksum = `XOR(bytes[0..6]) ^ 0xFF` (verify against SYNTREE spec).

**Mode 0 (Stroke)**: Command a specific pushrod position in mm. Best for mimicking pedal travel / ESTOP full brake / manual lever.
**Mode 1 (Pressure)**: Command hydraulic pressure in MPa. SEB's internal PID maintains target. Best for autonomous deceleration control (compensates for pad wear, temperature).

---

### 0x721 — SEB_STATUS (SYNTREE SEB Brake Feedback)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE SEB (brake module) |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 8 |
| **Period** | 10 ms (100 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SEB_Alignment_Status` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Alignment Info Feedback. 1 = aligned. (CSV Row 12) |
| `SEB_Control_Enable_Status` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Control Enable Feedback (CSV Row 13) |
| `SEB_Control_Mode_Status` | 2 | 2 | u8 | 1 | 0 | 0 | 3 | enum | Control Mode Feedback: 0=?, 1=Stroke?, 2=Pressure?, 3=? (CSV Row 14) |
| `SEB_AutoBrake_Status` | 4 | 1 | bool | 1 | 0 | 0 | 1 | — | Auto Brake Status Feedback (CSV Row 15) |
| (unaccounted) | 5 | 1 | — | — | — | — | — | — | Byte 0 bit 5 — not enumerated in CSV |
| `SEB_Error_Status` | 6 | 2 | u8 | 1 | 0 | 0 | 3 | enum | 0=No fault, 1=L1 minor, 2=L2 general, 3=L3 severe (CSV Row 16) |
| (unaccounted) | — | 8 | — | — | — | — | — | — | Byte 1 — not enumerated in CSV |
| `SEB_Stroke_Value` | 16 | 16 | u16 | 0.05 | -30 | -5 | 27 | mm | Stroke Value Feedback (CSV Row 17) |
| `SEB_Pressure_Value` | 24 | 8 | u8 | 0.05 | 0 | 0 | 5 | MPa | Pressure Value Feedback (CSV Row 18). Overlaps Stroke[15:8] at byte 3 — mode-dependent. |
| `SEB_Angle_Value` | 40 | 16 | i16 | 0.5 | 0 | -150 | 840 | — | Angle Feedback (CSV Row 19). Overlaps security echo bits at byte 6 — see note below. |
| `SEB_RollCnt_Enable_Status` | 48 | 1 | bool | 1 | 0 | 0 | 1 | — | Life Signal Status Feedback (CSV Row 20). Overlaps Angle_Value[15:8]. |
| `SEB_CheckSum_Enable_Status` | 49 | 1 | bool | 1 | 0 | 0 | 1 | — | Checksum Status Feedback (CSV Row 21). Overlaps Angle_Value[15:8]. |
| (unaccounted) | 50 | 2 | — | — | — | — | — | — | Byte 6 bits 2–3 — not enumerated in CSV |
| `SEB_RollCnt_Status` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Life Signal Feedback — echoes received rolling counter (CSV Row 22) |
| `SEB_CheckSum_Status` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum Feedback (CSV Row 23) |

**Byte layout** (little-endian, CSV as source of truth):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | Align[0]+CtrlEn[1]+Mode[2:3]+AutoBrk[4]+(gap)+Error[6:7] | (unacc.) | Stroke [7:0] | Stroke [15:8] / Pressure [7:0] (mode-muxed) | (unacc.) | Angle [7:0] | Angle[15:8] / RollCntEn[0]+CksEn[1]+(gap)+RollCnt[4:7] (overlap) | CksSum_Stat |

> **Byte 3 multiplexing:** Same pattern as command frame — `SEB_Stroke_Value` uses full 16-bit at bytes 2–3 in Stroke Mode; `SEB_Pressure_Value` uses byte 3 in Pressure Mode. The SEB reports whichever is active.
>
> **Byte 6 overlap:** CSV lists `SEB_Angle_Value` as 16-bit (bytes 5–6) AND security echo bits at byte 6 (bits 48–49, 52–55). These overlap. The CSV (manufacturer DBC export) declares both — they may represent different firmware versions or the Angle_Value may be 8-bit in practice (byte 5 only). Trust the CSV's declaration and handle in firmware by reading Angle as 16-bit from bytes 5–6, understanding that the upper byte may carry security echo data in some SEB firmware revisions.

**SYS usage**: Boot sync — read `SEB_Stroke_Value` as initial command target. Active — confirm `SEB_Alignment_Status == 1`. `SEB_Error_Status > 0` → log and report via `0x011`. Subscribe to `0x731 SEB_ErrInfo` for detailed fault flags — escalate L3 faults to ESTOP.

---

### 0x731 — SEB_ErrInfo (SYNTREE SEB Error Detail)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE SEB (brake module) |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 8 |
| **Period** | 100 ms (10 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

Detailed fault flags. Each bit is an independent fault indicator. 1 = fault active.

| Signal | Start bit | Len | Type | Fault Level | Description |
|--------|-----------|-----|------|-------------|-------------|
| `SEB_ECUUnderVolt_Err` | 0 | 1 | bool | L2 | Controller Undervoltage |
| `SEB_ECUOverVolt_Err` | 1 | 1 | bool | L2 | Controller Overvoltage |
| `SEB_CanCom_Err` | 2 | 1 | bool | **L3** | CAN Communication Fault |
| `SEB_ECUTemp_Err` | 3 | 1 | bool | **L3** | Controller Temperature Fault |
| `SEB_Domain_drive_SC_Err` | 4 | 1 | bool | **L3** | Domain Drive Short Circuit |
| `SEB_Domain_drive_V_Err` | 5 | 1 | bool | **L3** | Domain Drive Voltage Fault |
| `SEB_Domain_drive_T_Err` | 6 | 1 | bool | **L3** | Domain Drive Temperature Fault |
| `SEB_AngleSensor_P_OC_Err` | 7 | 1 | bool | **L3** | Angle Sensor P Open Circuit |
| `SEB_AngleSensor_P_AF_Err` | 8 | 1 | bool | **L3** | Angle Sensor P Mainboard Abnormal |
| `SEB_AngleSensor_S_OC_Err` | 9 | 1 | bool | **L3** | Angle Sensor S Open Circuit |
| `SEB_AngleSensor_S_AF_Err` | 10 | 1 | bool | **L3** | Angle Sensor S Sub-board Abnormal |
| `SEB_NOPreSensor_Err` | 11 | 1 | bool | **L3** | Unconnected Oil Pressure Sensor |
| (reserved) | 12 | 1 | — | — | |
| `SEB_SensorUCL_Err` | 13 | 1 | bool | **L3** | Sensor Plausibility Fault |
| `SEB_Alignment_Err` | 14 | 1 | bool | L2 | Alignment Fault |
| `SEB_AngleOver_Err` | 15 | 1 | bool | L2 | Angle Out of Bounds |
| (reserved) | 16 | 1 | — | — | |
| `SEB_Mtr_Stall_Err` | 17 | 1 | bool | **L3** | Motor Stall Fault |
| `SEB_MtrDC_Err` | 18 | 1 | bool | **L3** | Motor Disconnect Fault |
| `SEB_Oil_Err` | 19 | 1 | bool | L2 | Oil Pressure Error |
| `SEB_InitOil_Err` | 20 | 1 | bool | **L3** | Initial Oil Pressure Fault |
| `SEB_SentValue_Err` | 21 | 1 | bool | **L3** | Send Value Error |
| `SEB_Mtr_NoLoad_Err` | 22 | 1 | bool | **L3** | Motor No-load Fault |
| (reserved) | 23 | 1 | — | — | |
| `SEB_PreSensorOver_Err` | 24 | 1 | bool | L2 | Oil Pressure Sensor Overvoltage |
| `SEB_LowVolt_Charging_Err` | 25 | 1 | bool | L2 | Low Voltage Charging Failure |
| (reserved) | 26 | 38 | — | — | Bits 26–63 (byte 3 bits 2–7 + bytes 4–7). Not enumerated in CSV. |

**Byte layout** (little-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | faults [7:0] | faults [15:8] | faults [23:16] | faults [31:24] | rsvd | rsvd | rsvd | rsvd |

> **Safety note:** 14 of 23 documented faults are L3 (severe, request shutdown). SYS must subscribe to this message and escalate any L3 fault to ESTOP via `0x001`. The summary `SEB_Error_Status` in `0x721` only provides a 2-bit aggregate level; this message reveals *which* sensor or subsystem failed and at what severity.

---

### 0x741 — SEB_Version (SYNTREE SEB Firmware Version)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE SEB (brake module) |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 8 |
| **Period** | 1000 ms (1 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SEB_SW_Version` | 0 | 8 | u8 | 0.01 | 0 | 0 | 25.5 | — | Software version (e.g., 0xC8 = 2.00) |
| `SEB_HW_Version` | 8 | 8 | u8 | 0.1 | 0 | 0 | 25.5 | — | Hardware version (e.g., 0x0D = 1.3) |
| (reserved) | 16 | 48 | — | — | — | — | — | — | Bytes 2–7 |

**SYS usage**: Log on boot for compatibility check and field diagnostics. Report via `0x600 SYS_DIAG_RPT`.

---

### 0x6FB — SEB_Test (SYNTREE SEB Telemetry)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE SEB (brake module) |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 8 |
| **Period** | 10 ms (100 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| (reserved) | 0 | 8 | — | — | — | — | — | — | Byte 0 |
| `SEB_MtrCurr` | 8 | 16 | i16 | 0.0078125 | 0 | -255 | 255 | A | Motor current. Bytes 1–2. ±1.99 A range. |
| `SEB_ECUTemp` | 24 | 16 | u16 | 0.5 | 0 | -40 | 215 | °C | ECU temperature. Bytes 3–4. |
| `SEB_PowVolt` | 40 | 16 | u16 | 0.00390625 | 0 | 0 | 32 | V | Power supply voltage. Bytes 5–6. |
| (reserved) | 56 | 8 | — | — | — | — | — | — | Byte 7 |

> **Note:** CSV uses byte-local start-bit numbering for this message (start bit = offset within start byte). Converted to absolute Motorola LSB above. **SYS usage:** Monitor `SEB_MtrCurr` for mechanical binding / motor degradation trends. `SEB_ECUTemp` for over-temperature early warning.

---

### 0x7FD — RT_HEARTBEAT (low-level)

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | SYS |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1000ms (2 missed frames) → SYS triggers ESTOP (AUTO only) |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung RT. |

`0x204` staleness check at 200ms provides faster detection. RT sends `0x7FD` independently on both buses (per-bus, NOT bridged; separate counters per bus).

---

### 0x7FE — SYS_HEARTBEAT (low-level)

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1000ms (2 missed frames) → RT sends CAN `0x001` ESTOP (AUTO only) |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung SYS. |

SYS heartbeat never leaves low bus. Startup grace period: 3 seconds (both heartbeat monitors).

---

## 2. High-Level CAN Bus

Nodes: Jetson Orin, RT ESP32-S3 (MCP2515 SPI).

---

### 0x001 — SAFETY_ESTOP (high-level)

| Property | Value |
|----------|-------|
| **Sender** | Jetson or RT (fwd from low) |
| **Receiver(s)** | Jetson, RT |
| **DLC** | 0 |
| **Period** | On event |

Bridged by RT between buses.

---

### 0x011 — SYS_SAFETY_STS (forwarded)

Forwarded from low-level by RT. Same payload layout as §1 `0x011`.

---

### 0x120 — SYS_THROTTLE_STS (forwarded)

Forwarded from low-level by RT. Same payload layout as §1 `0x120`.

---

### 0x210 — RT_STATE_RPT

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 3 |
| **Period** | 10 Hz |

| Signal | Start bit | Len | Type | Min | Max | Unit |
|--------|-----------|-----|------|-----|-----|------|
| `RT_Mode` | 0 | 8 | u8 (enum) | 0 | 2 | — |
| `RT_SteerValid` | 8 | 8 | u8 (bool) | 0 | 1 | — |
| `RT_Reversing` | 16 | 8 | u8 (bool) | 0 | 1 | — |

Byte layout (big-endian): Byte 0=mode, 1=steer_valid, 2=reversing.

---

### 0x220 — RT_PID_RPT

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 6 |
| **Period** | 10 Hz |

| Signal | Start bit | Len | Type | Unit |
|--------|-----------|-----|------|------|
| `RT_PidSetpoint` | 0 | 16 | i16 | mm/s |
| `RT_PidMeasured` | 16 | 16 | i16 | mm/s |
| `RT_PidOutput` | 32 | 16 | i16 | — |

Byte layout (big-endian): Bytes 0-1=sp, 2-3=meas, 4-5=out.

---

### 0x300 — HOST_DRIVE_CMD

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT |
| **DLC** | 8 |
| **Period** | ≤100 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `HOST_DriveSpeed` | 0 | 32 | i32 | 1 | 0 | -500 | 3000 | mm/s |
| `HOST_YawRate` | 32 | 24 | i24 | 1 | 0 | -3000 | 3000 | mrad/s |
| `HOST_Gear` | 56 | 8 | u8 | 1 | 0 | 0 | 3 | enum (0=N,1=D,2=S,3=R) |

Byte layout (big-endian): Bytes 0-3=speed, 4-6=yaw[23:0], 7=gear.

ROS 2 conversion: `speed_mmps = linear.x × 1000`, `yaw_rate_mrad_s = angular.z × 1000`.

---

### 0x301 — HOST_BRAKE_REQ

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT |
| **DLC** | 4 |
| **Period** | On demand |

| Signal | Start bit | Len | Type | Unit |
|--------|-----------|-----|------|------|
| `HOST_BrakePressure` | 0 | 32 | i32 | kPa |

Byte layout (big-endian): Bytes 0-3. RT arbitrates: max(RT_computed, HOST_request). Result forwarded to SYS via `0x205` RT_BRAKE_CMD (i32 kPa, 50 Hz).

---

### 0x302 — HOST_LIGHT_CMD

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT (→ SYS) |
| **DLC** | 1 |
| **Period** | On change |

Layout identical to low-level `0x302`. RT forwards transparently.

---

### 0x400 — RT_OBSTACLE_RPT

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 4 |
| **Period** | 10 Hz |

| Signal | Start bit | Len | Type | Min | Max | Unit |
|--------|-----------|-----|------|-----|-----|------|
| `RT_ObstacleDistance` | 0 | 32 | u32 | 0 | 2³²−1 | mm |

UINT32_MAX = no reading / timeout.

---

### 0x600 — SYS_DIAG_RPT (forwarded)

Forwarded from low-level by RT. Same layout as §1 `0x600`.

---

### 0x7FD — RT_HEARTBEAT (high-level)

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1500ms (3 missed frames) → Jetson stops publishing `/cmd_vel` |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung RT. |

RT sends `0x7FD` independently on both buses (per-bus, NOT bridged).

---

### 0x7FC — JETSON_HEARTBEAT (high-level)

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1500ms (3 missed frames) → RT zeroes `0x204` + stops `0x169` (controlled stop) |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung Jetson. |

Jetson is QM, not safety-critical. Heartbeat loss triggers controlled stop, not ESTOP.

---

## 3. CAN ID Summary

### Low-level bus

| ID | Name | Sender | Receiver | DLC | Rate |
|----|------|--------|----------|-----|------|
| `0x001` | SAFETY_ESTOP | RT, SYS | All | 0 | Event |
| `0x011` | SYS_SAFETY_STS | SYS | RT (→Jetson) | 2 | 5 Hz |
| `0x012` | SYS_DCDC_CMD | SYS | DC-DC | 1 | Change |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | Change |
| `0x120` | SYS_THROTTLE_STS | SYS | RT (→Jetson) | 2 | 100 Hz |
| `0x169` | VCU_SES_REQ | RT | EPS-C | 8 | **50 Hz** |
| `0x201` | SES_STATUS | EPS-C | RT | 8 | 100 Hz |
| `0x202` | SES_ErrInfo | EPS-C | RT | 8 | 10 Hz |
| `0x203` | SES_Version | EPS-C | RT | 8 | 1 Hz |
| `0x204` | RT_DRIVE_CMD | RT | SYS | 5 | 100 Hz |
| `0x205` | RT_BRAKE_CMD | RT | SYS | 4 | **50 Hz** |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | SYS | 1 | Change |
| `0x600` | SYS_DIAG_RPT | SYS | RT (→Jetson) | 8 | 1 Hz |
| `0x6FA` | SES_Test | EPS-C | RT | 8 | 100 Hz |
| `0x6FB` | SEB_Test | SEB | SYS | 8 | 100 Hz |
| `0x721` | SEB_STATUS | SEB | SYS | 8 | 100 Hz |
| `0x731` | SEB_ErrInfo | SEB | SYS | 8 | 10 Hz |
| `0x741` | SEB_Version | SEB | SYS | 8 | 1 Hz |
| `0x7B9` | VCU_SEB_REQ | SYS | SEB | 8 | **50 Hz** |
| `0x7FD` | RT_HEARTBEAT | RT | SYS | 1 | 2 Hz |
| `0x7FE` | SYS_HEARTBEAT | SYS | RT | 1 | 2 Hz |

### High-level bus

| ID | Name | Sender | Receiver | DLC | Rate |
|----|------|--------|----------|-----|------|
| `0x001` | SAFETY_ESTOP | Jetson, RT | Jetson, RT | 0 | Event |
| `0x011` | SYS_SAFETY_STS | RT (fwd) | Jetson | 2 | 5 Hz |
| `0x120` | SYS_THROTTLE_STS | RT (fwd) | Jetson | 2 | 100 Hz |
| `0x210` | RT_STATE_RPT | RT | Jetson | 3 | 10 Hz |
| `0x220` | RT_PID_RPT | RT | Jetson | 6 | 10 Hz |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQ | Jetson | RT | 4 | Demand |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→SYS) | 1 | Change |
| `0x400` | RT_OBSTACLE_RPT | RT | Jetson | 4 | 10 Hz |
| `0x600` | SYS_DIAG_RPT | RT (fwd) | Jetson | 8 | 1 Hz |
| `0x7FD` | RT_HEARTBEAT | RT | Jetson | 1 | 2 Hz |
| `0x7FC` | JETSON_HEARTBEAT | Jetson | RT | 1 | 2 Hz |

---

## 4. Forwarding Rules (RT Gateway)

RT is the only dual-bus node. Every CAN message falls into exactly one of three categories:

### Category 1: Transparent forward (same ID, same payload)

| Direction | IDs |
|-----------|-----|
| Low → High | `0x001`, `0x011`, `0x120`, `0x600` |
| High → Low | `0x001`, `0x302` |

### Category 2: Consumed by RT → different message generated

| Inbound | Bus | Outbound | Bus |
|---------|-----|----------|-----|
| `0x300` HOST_DRIVE_CMD | High | `0x204` RT_DRIVE_CMD + `0x169` VCU_SES_REQ | Low |
| `0x301` HOST_BRAKE_REQ | High | `0x205` RT_BRAKE_CMD | Low |

### Category 3: Bus-local (never forwarded, never regenerated)

| Bus | IDs |
|-----|-----|
| Low only | `0x012`, `0x110`, `0x169`, `0x202`, `0x203`, `0x204`, `0x205`, `0x6FA`, `0x6FB`, `0x721`, `0x731`, `0x741`, `0x7B9` |
| Low only | `0x201` (SYNTREE EPS-C feedback) |
| High only | `0x210`, `0x220`, `0x400` (RT telemetry) |
| Both independent | `0x7FD`, `0x7FE`, `0x7FC` (per-node heartbeat — NOT bridged) |

---

## 5. Priority Groups

| Priority | ID Range | IDs |
|----------|----------|-----|
| Highest | `0x001` | ESTOP |
| Very High | `0x010`–`0x01F` | SAFETY_STATUS, DCDC_CMD |
| High | `0x100`–`0x11F` | MODE_CMD |
| Medium | `0x120`–`0x3FF` | THROTTLE, DRIVE, SES_STATUS/REQ/ErrInfo/Version, DRIVE_CMD, BRAKE_REQ, LIGHT_CMD |
| Low | `0x400`–`0x5FF` | OBSTACLE, STATE_REPORT, PID_FEEDBACK |
| Lowest | `0x600`–`0x7FF` | DIAG, SES_Test (`0x6FA`), SEB_Test (`0x6FB`), SEB_STATUS/ErrInfo/Version (`0x721`/`0x731`/`0x741`), SEB_REQ (`0x7B9`), HEARTBEAT (`0x7FC`–`0x7FE`) |

> Lower CAN ID = higher bus arbitration priority. Safety-critical frames occupy `0x00X`. SYNTREE IDs (`0x2XX`, `0x7XX`) are in medium/lowest ranges per manufacturer assignment.
